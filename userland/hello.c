#define SYSCALL_READ 0
#define SYSCALL_WRITE 1
#define SYSCALL_MMAP 9
#define SYSCALL_MUNMAP 11
#define SYSCALL_BRK 12
#define SYSCALL_GETPID 39
#define SYSCALL_FORK 57
#define SYSCALL_WAIT4 61
#define SYSCALL_EXECVE 59
#define SYSCALL_EXIT_GROUP 231

#define PROTECTION_READ 1
#define PROTECTION_WRITE 2
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20

#define EXIT_SUCCESS_CODE 42
#define EXIT_WRITE_FAILED 10
#define EXIT_PROCESS_ID_FAILED 11
#define EXIT_MMAP_FAILED 12
#define EXIT_MMAP_UNUSABLE 13
#define EXIT_BRK_FAILED 14
#define EXIT_HEAP_UNUSABLE 15
#define EXIT_MUNMAP_FAILED 16
#define EXIT_FORK_FAILED 17
#define EXIT_WAIT_FAILED 18
#define CHILD_EXIT_CODE 7
#define EXIT_EXECVE_FAILED 19
#define EXEC_CHILD_EXIT_CODE 23

typedef long Word;

static inline Word invoke(Word number, Word first, Word second, Word third, Word fourth,
                          Word fifth, Word sixth) {
    Word result;
    register Word r10 __asm__("r10") = fourth;
    register Word r8 __asm__("r8") = fifth;
    register Word r9 __asm__("r9") = sixth;

    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(first), "S"(second), "d"(third), "r"(r10), "r"(r8),
                       "r"(r9)
                     : "rcx", "r11", "memory");
    return result;
}

static Word write_text(const char *text, Word length) {
    return invoke(SYSCALL_WRITE, 1, (Word)text, length, 0, 0, 0);
}

static void leave(Word status) {
    invoke(SYSCALL_EXIT_GROUP, status, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

static Word text_length(const char *text) {
    Word length = 0;
    while (text[length] != '\0')
        ++length;
    return length;
}

static void expect(int condition, Word failure_status) {
    if (!condition)
        leave(failure_status);
}

static const char GREETING[] = "hello from ring 3\n";
static const char CHILD_MESSAGE[] = "child process reporting\n";
static const char EXEC_MESSAGE[] = "replaced image reporting\n";
static const char EXEC_FLAG[] = "--exec-child";
static const char PROGRAM_PATH[] = "/bin/hello";
static const char REPORT[] = "user space checks passed\n";

static int text_equals(const char *left, const char *right) {
    Word index = 0;
    while (left[index] != '\0' && left[index] == right[index])
        ++index;
    return left[index] == right[index];
}

void barry_user_main(Word *stack);

__asm__(".globl _start\n"
        "_start:\n"
        "  xorl %ebp, %ebp\n"
        "  movq %rsp, %rdi\n"
        "  andq $-16, %rsp\n"
        "  call barry_user_main\n"
        "  hlt\n");

void barry_user_main(Word *stack) {
    Word const argument_count = stack[0];
    const char *const *arguments = (const char *const *)&stack[1];

    if (argument_count > 1 && text_equals(arguments[1], EXEC_FLAG)) {
        Word const exec_length = text_length(EXEC_MESSAGE);
        expect(write_text(EXEC_MESSAGE, exec_length) == exec_length, EXIT_WRITE_FAILED);
        leave(EXEC_CHILD_EXIT_CODE);
    }

    Word const greeting_length = text_length(GREETING);
    expect(write_text(GREETING, greeting_length) == greeting_length, EXIT_WRITE_FAILED);

    Word const process_id = invoke(SYSCALL_GETPID, 0, 0, 0, 0, 0, 0);
    expect(process_id > 0, EXIT_PROCESS_ID_FAILED);

    Word const page_size = 4096;
    Word const mapping = invoke(SYSCALL_MMAP, 0, page_size, PROTECTION_READ | PROTECTION_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    expect(mapping > 0, EXIT_MMAP_FAILED);

    volatile unsigned long *probe = (volatile unsigned long *)mapping;
    probe[0] = 0x1234abcdUL;
    probe[page_size / sizeof(unsigned long) - 1] = 0x5678ef01UL;
    expect(probe[0] == 0x1234abcdUL, EXIT_MMAP_UNUSABLE);
    expect(probe[page_size / sizeof(unsigned long) - 1] == 0x5678ef01UL, EXIT_MMAP_UNUSABLE);

    Word const original_break = invoke(SYSCALL_BRK, 0, 0, 0, 0, 0, 0);
    expect(original_break > 0, EXIT_BRK_FAILED);

    Word const grown_break = invoke(SYSCALL_BRK, original_break + 2 * page_size, 0, 0, 0, 0, 0);
    expect(grown_break >= original_break + 2 * page_size, EXIT_BRK_FAILED);

    volatile unsigned char *heap = (volatile unsigned char *)original_break;
    heap[0] = 7;
    heap[page_size] = 9;
    expect(heap[0] == 7, EXIT_HEAP_UNUSABLE);
    expect(heap[page_size] == 9, EXIT_HEAP_UNUSABLE);

    expect(invoke(SYSCALL_MUNMAP, mapping, page_size, 0, 0, 0, 0) == 0, EXIT_MUNMAP_FAILED);

    Word const child = invoke(SYSCALL_FORK, 0, 0, 0, 0, 0, 0);
    expect(child >= 0, EXIT_FORK_FAILED);

    if (child == 0) {
        Word const child_length = text_length(CHILD_MESSAGE);
        expect(write_text(CHILD_MESSAGE, child_length) == child_length, EXIT_WRITE_FAILED);
        leave(CHILD_EXIT_CODE);
    }

    int status = 0;
    Word const waited = invoke(SYSCALL_WAIT4, child, (Word)&status, 0, 0, 0, 0);
    expect(waited == child, EXIT_WAIT_FAILED);
    expect(((status >> 8) & 0xff) == CHILD_EXIT_CODE, EXIT_WAIT_FAILED);

    Word const exec_child = invoke(SYSCALL_FORK, 0, 0, 0, 0, 0, 0);
    expect(exec_child >= 0, EXIT_FORK_FAILED);

    if (exec_child == 0) {
        const char *exec_arguments[3];
        const char *exec_environment[1];
        exec_arguments[0] = PROGRAM_PATH;
        exec_arguments[1] = EXEC_FLAG;
        exec_arguments[2] = 0;
        exec_environment[0] = 0;

        invoke(SYSCALL_EXECVE, (Word)PROGRAM_PATH, (Word)exec_arguments,
               (Word)exec_environment, 0, 0, 0);
        leave(EXIT_EXECVE_FAILED);
    }

    int exec_status = 0;
    Word const exec_waited = invoke(SYSCALL_WAIT4, exec_child, (Word)&exec_status, 0, 0, 0, 0);
    expect(exec_waited == exec_child, EXIT_WAIT_FAILED);
    expect(((exec_status >> 8) & 0xff) == EXEC_CHILD_EXIT_CODE, EXIT_WAIT_FAILED);

    Word const report_length = text_length(REPORT);
    expect(write_text(REPORT, report_length) == report_length, EXIT_WRITE_FAILED);

    leave(EXIT_SUCCESS_CODE);
}
