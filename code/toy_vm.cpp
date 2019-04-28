#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>
#include <assert.h>

// 寄存器编号
enum {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,                   // PC 寄存器
    R_COND,                 // 标志寄存器
    R_COUNT,                // 10
};

// 每次计算完都要更新 R_COND
enum {
    FL_POS = 1 << 0,        // > 0
    FL_ZEO = 1 << 1,        // = 0
    FL_NEG = 1 << 2,        // < 0
};

// 操作码编号
enum {
    OP_BR = 0,              // branch
    OP_ADD,                 // add 
    OP_LD,                  // load
    OP_ST,                  // store
    OP_JSR,                 // jump register
    OP_AND,                 // bitwise and
    OP_LDR,                 // load register
    OP_STR,                 // store register
    OP_RTI,                 // unused 
    OP_NOT,                 // bitwise not
    OP_LDI,                 // load indirect
    OP_STI,                 // store indirect
    OP_JMP,                 // jump
    OP_RES,                 // reserved (unused)
    OP_LEA,                 // load effective address
    OP_TRAP                 // execute trap
};

// 内存映射寄存器代码地址
enum {
    MR_KBSR = 0xFE00,       // keyboard status
    MR_KBDR = 0xFE02        // keyboard data
};


// 陷入例程编号
enum {
    TRAP_GETC   = 0x20,     // get character from keyboard, not echoed onto the terminal
    TRAP_OUT    = 0x21,     // output a character
    TRAP_PUTS   = 0x22,     // output a word string
    TRAP_IN     = 0x23,     // get character from keyboard, echoed onto the terminal
    TRAP_PUTSP  = 0x24,     // output a byte string
    TRAP_HALT   = 0x25,     // halt the program
};

// 内存
uint16_t memory[UINT16_MAX];

// 寄存器
uint16_t regs[R_COUNT];
struct termios original_tio;
int running = 1;

int read_image(const char *image_path);
void read_image_file(FILE *file);
uint16_t swap16(uint16_t x);
uint16_t check_key();
void disable_input_buffering();
void restore_input_buffering();
void handle_interrupt(int signal);
void mem_write(uint16_t address, uint16_t value);
uint16_t mem_read(uint16_t addrress);
void update_flag(uint16_t);
// 定义 op 函数
void ins(uint16_t);
uint16_t sign_extend(uint16_t, int);

void output_assert(int, const char *);
void test();
void reset();

int main(int argc, const char *argv[]) {

    test();
    // if(argc < 2) {
    //     printf("缺少文件地址");
    //     exit(2);
    // }

    // // 将文件读入内存
    // for(int i = 1; i < argc; i++) {
    //     if(!read_image(argv[i])) {
    //         printf("没有找到 image 文件");
    //         exit(1);
    //     }
    // }

    // // 终端

    // // Setup
    // signal(SIGINT, handle_interrupt);
    // disable_input_buffering();

    // enum {
    //     PC_START = 0x3000
    // };
    // regs[R_PC] = PC_START;

    // // 取址执行
    // while (running) {
    //     uint16_t instr = mem_read(regs[R_PC]++);
    //     ins(instr);
    // }

    // // Shut Down
    // restore_input_buffering();
    return 0;
}

void update_flag(uint16_t result) {
    if(result > 0) {
        regs[R_COND] = FL_POS;
    } else if(result < 0) {
        regs[R_COND] = FL_NEG;
    } else {
        regs[R_COND] = FL_ZEO;
    }
    
}

// 套路有符号拓展函数
uint16_t sign_extend(uint16_t x, int bit_count) {
    // 把有符号和无符号拓展写在一起
    if((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
} 
void ins(uint16_t instr) {
    // 记住要更新 flag
    uint16_t op = instr >> 12;
    // printf("op %u \n", op);
    switch (op) {
        case OP_ADD:
            {
                uint16_t dr = (instr >> 9) & 0b111;
                uint16_t sr = (instr >> 6) & 0b111;

                uint16_t imm_flag = (instr >> 5) & 0b1;

                if(imm_flag) {
                    regs[dr] = regs[sr] + sign_extend(instr & 0b11111, 5);
                } else {
                    uint16_t sr1 = instr & 0b111;
                    regs[dr] = regs[sr] + regs[sr1];
                }
                update_flag(regs[dr]);
            }
            break;

        case OP_AND: 
            {
                uint16_t dr = (instr >> 9) & 0b111;
                uint16_t sr = (instr >> 6) & 0b111;

                uint16_t imm_flag = (instr >> 5) & 0b1;

                if(imm_flag) {
                    regs[dr] = regs[sr] & sign_extend(instr & 0b11111, 5);
                } else {
                    uint16_t sr1 = instr & 0b111;
                    regs[dr] = regs[sr] & regs[sr1];
                }
                update_flag(regs[dr]);
            }
            break;

        case OP_BR:
            {
                uint16_t cond_flag = (instr >> 9) & 0b111;
                if(cond_flag & regs[R_COND]) {
                    uint16_t pc_offset = sign_extend(instr & 0b111111111, 9);
                    regs[R_PC] += pc_offset;
                }
            }
            break;
        case OP_JMP:
            {
                uint16_t base_r = (instr >> 6) & 0b111;
                regs[R_PC] = base_r;
            }
            break;
        case OP_JSR:
            {
                uint16_t flag = (instr >> 11) & 0b1;
                if(flag) {
                    // JSR
                    uint16_t pc_offset = sign_extend(instr & 0x7FF, 11);
                    regs[R_PC] += pc_offset;
                }else {
                    // JSRR
                    uint16_t base_r = (instr >> 6) & 0x7;
                    regs[R_PC] = base_r; 
                }
            }
            break;

        case OP_LD:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                uint16_t address = regs[R_PC] + pc_offset; 
                regs[dr] = mem_read(address);
                update_flag(regs[dr]);
            }
            break;
        case OP_LDI:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                uint16_t add = regs[R_PC] + pc_offset;
                uint16_t add1 = mem_read(add);
                regs[dr] = mem_read(add1);
                update_flag(regs[dr]);
            }
            break;
        case OP_LDR:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t base_r = (instr >> 6) & 0b111;
                uint16_t pc_offset = sign_extend(instr & 0x3F, 6);
                regs[dr] = mem_read(base_r + pc_offset);
                update_flag(regs[dr]);
            }
            break;
        case OP_LEA:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                regs[dr] = regs[R_PC] + pc_offset;
                update_flag(regs[dr]);
            }
            break;

        case OP_NOT:
            {
                uint16_t dr = (instr >> 9) & 0x7;
                uint16_t sr = (instr >> 6) & 0x7;

                regs[dr] = ~regs[sr];
                update_flag(regs[dr]);
            }
            break;

        case OP_ST:
            {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                uint16_t add = regs[R_PC] + pc_offset;
                mem_write(add, regs[sr]);
            }
            break;

        case OP_STI:
            {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                uint16_t add = regs[R_PC] + pc_offset;
                uint16_t add1 = mem_read(add);
                mem_write(add1, regs[sr]);
            }
            break;

        case OP_STR:
            {
                uint16_t sr = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x3F, 6);
                uint16_t base_r = (instr >> 6) & 0x7;
                mem_write(base_r + pc_offset, regs[sr]);
            }
            break;
        case OP_TRAP:
            // trap code system call
            {
                uint16_t trap_vect = instr & 0xFF;
                switch (trap_vect)
                {
                    case TRAP_GETC:
                        {
                            regs[R_R0] = (uint16_t)getchar();
                        }
                        break;
                    
                    case TRAP_OUT:
                        // 放一个字符在 stdout 中
                        {
                            putc((char)regs[R_R0], stdout);
                            fflush(stdout);
                        }
                        break;

                    case TRAP_PUTS:
                        // 写一串字符
                        {
                            uint16_t *c = memory + regs[R_R0];
                            while(*c) {
                                putc((char)*c, stdout);
                                ++c;
                            }
                            fflush(stdout);
                        }
                        break;

                    case TRAP_IN:
                        {
                            printf("Enter a character: ");
                            char c = getchar();
                            putc(c, stdout);
                            regs[R_R0] = (uint16_t)c;
                        }
                        break;
                    case TRAP_PUTSP:
                        // Write a string of ASCII characters to the console.
                        // 先输出右八位 再输出左边八位
                        // 如果这个字符串是奇数字符串，那么最后一个左八位 是 0
                        {
                            uint16_t *c = memory + regs[R_R0];

                            while(*c) {
                                char cha = (*c) & 0xFF;
                                putc(cha, stdout);
                                char cha1 = (*c) >> 8;
                                if(cha1) {
                                    putc(cha, stdout);
                                }
                                c++;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_HALT:
                        {
                            puts("HALT");
                            fflush(stdout);
                            running = 0;
                        }
                        break;
                    default:
                        break;
                }


            } 
            break;

        case OP_RES:
        case OP_RTI:
        default:
            // bad code
            abort();
            break;
    }
    

}

void reset() {
    for(int i = 0; i < R_COUNT; i++) {
        regs[i] = i;
    }

    for(int i = 0; i < 65535; i++) {
        memory[i] = 0;
    }
}
void output_assert(int expression, const char *s) {
    assert(expression);
    printf("%s\n", s);
}

void test() {
    uint16_t instr;
    printf("开始测试：\n");
    {
        // ADD
        regs[1] = 0;
        regs[2] = 1;
        regs[3] = 2;
        instr = 0b0001001010000011;
        ins(instr);
        assert(regs[1] == 3);
        reset();

        instr = 0b0001001111101000;
        regs[1] = 0;
        regs[7] = 10;
        ins(instr);
        output_assert(regs[1] == 18, "OP_ADD √");
        reset();
    }
    {
        // AND
        instr = 0b0101000001000010;
        ins(instr);
        assert(regs[0] == 0);
        reset();
        instr = 0b0101001111101000;
        ins(instr);
        output_assert(regs[1] == 0, "OP_AND √");
        reset();
    }
    {
        // BR
        regs[R_PC] = 10;
        regs[R_COND] = 0b100;
        instr = 0b0000100000000010;
        ins(instr);
        output_assert(regs[R_PC] == 12, "OP_BR √");
        reset();
    }
    {
        // JMP
        instr = 0b1100000111000010;
        ins(instr);
        output_assert(regs[R_PC] == 7, "OP_JMP √");
        reset();
    }
    {
        // JSR
        instr = 0b0100100000000100;
        ins(instr);
        output_assert(regs[R_PC] == 12, "OP_JSR √");
        reset();

        // JSRR
        instr = 0b0100000111000000;
        ins(instr);
        output_assert(regs[R_PC] == 7, "OP_JSRR √");
        reset();
    }
    {
        // LD
        instr = 0b0010111000000011;
        regs[R_PC] = 1;
        uint16_t add = regs[R_PC] + sign_extend(3, 9);
        memory[add] = 10;
        ins(instr);
        output_assert(regs[7] == 10, "OP_LD √");
        reset();
    }
    {
        // LDI
        instr = 0b1010111000000010;
        regs[R_PC] = 1;
        uint16_t add = regs[R_PC] + sign_extend(2, 9);
        mem_write(add, 10);
        uint16_t add1 = mem_read(add);
        mem_write(add1, 100);
        ins(instr);
        output_assert(regs[7] == 100, "OP_LDI √");
        reset();
    }
    {
        // LDR
        instr = 0b0110111111000010;
        uint16_t add = 9;
        mem_write(add, 10);
        ins(instr);
        output_assert(regs[7] == 10, "OP_LDR √");
        reset();
    }
    {
        // LEA
        instr = 0b1110111000000010;
        regs[R_PC] = 10;
        ins(instr);
        output_assert(regs[7] == 12, "OP_LEA √");
        reset();
    }
    {   
        // NOT
        instr = 0b1001111000111111;
        regs[0] = 0b1111111111111111;
        ins(instr);
        output_assert(regs[7] == 0, "OP_NOT √");
        reset();
    }
    {
        // ST
        instr = 0b0011111000000001;
        regs[7] = 7;
        regs[R_PC] = 1;
        uint16_t add = regs[R_PC] + sign_extend(1, 9);
        ins(instr);
        output_assert(regs[7] == mem_read(add), "OP_ST √");
        reset();
    }
    {
        // STI
        instr = 0b1011111000000001;
        regs[7] = 7;
        regs[R_PC] = 1;
        uint16_t add = regs[R_PC] + sign_extend(1, 9);
        mem_write(add, 10);
        ins(instr);
        output_assert(regs[7] == mem_read(10), "OP_STI √");
        reset();
    }
    {
        // STR
        instr = 0b0111111000000001;
        regs[7] = 7;
        uint16_t add = 0 + sign_extend(1, 6);
        ins(instr);
        output_assert(regs[7] == mem_read(add), "OP_STR √");
        reset();
    }
}

int read_image(const char *image_path) {

    FILE *file = fopen(image_path, "rb");
    if(!file) {
        return 0;
    }
    read_image_file(file);
    fclose(file);
    return 1;
}

uint16_t swap16(uint16_t x) {
    return (x >> 8) | (x << 8);
}


void read_image_file(FILE *file) {

    // 文件开头 16 位是地址
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);

    // 程序是大端机器写的，所以每个读出来的内容都要转换
    origin = swap16(origin);

    uint16_t max_read = UINT16_MAX - origin;
    uint16_t *p = memory + origin;

    // 读入内存
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    // 把所有的内容转换成小端
    while(read--) {
        *p = swap16(*p);
        p++;
    }
}

uint16_t check_key() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

void disable_input_buffering() {
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal) {
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

void mem_write(uint16_t address, uint16_t value) {
    memory[address] = value;
}
uint16_t mem_read(uint16_t address) {
    // address 不可能超过 65535
    if(address == MR_KBSR) {
        // 特殊的内存寄存器映射

        if(check_key()) {
            // 轮循接受字符
            // 也就是说，可以响应式接受字符
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        } else {
            memory[MR_KBDR] = 0;    
        }
    }
    return memory[address];
}
