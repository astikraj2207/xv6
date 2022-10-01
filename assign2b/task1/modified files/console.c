// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "history.h"

// Stores command history
char history_buffer[MAX_HISTORY][MAX_BUFFER_LEN];
// Number of commands in history
int histCnt = 0;
// Current index in history array
int currCnt = 0;

static void consputc(int);

static int panicked = 0;

static struct {
    struct spinlock lock;
    int locking;
} cons;

// Forward declarations for history functions
int sys_history(void);
void add_to_history(char *);

static void printint(int xx, int base, int sign) {
    static char digits[] = "0123456789abcdef";
    char buf[16];
    int i;
    uint x;

    if (sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;

    i = 0;
    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign) buf[i++] = '-';

    while (--i >= 0) consputc(buf[i]);
}
// PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void cprintf(char *fmt, ...) {
    int i, c, locking;
    uint *argp;
    char *s;

    locking = cons.locking;
    if (locking) acquire(&cons.lock);

    if (fmt == 0) panic("null fmt");

    argp = (uint *)(void *)(&fmt + 1);
    for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
        if (c != '%') {
            consputc(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if (c == 0) break;
        switch (c) {
            case 'd':
                printint(*argp++, 10, 1);
                break;
            case 'x':
            case 'p':
                printint(*argp++, 16, 0);
                break;
            case 's':
                if ((s = (char *)*argp++) == 0) s = "(null)";
                for (; *s; s++) consputc(*s);
                break;
            case '%':
                consputc('%');
                break;
            default:
                // Print unknown % sequence to draw attention.
                consputc('%');
                consputc(c);
                break;
        }
    }

    if (locking) release(&cons.lock);
}

void panic(char *s) {
    int i;
    uint pcs[10];

    cli();
    cons.locking = 0;
    // use lapiccpunum so that we can call panic from mycpu()
    cprintf("lapicid %d: panic: ", lapicid());
    cprintf(s);
    cprintf("\n");
    getcallerpcs(&s, pcs);
    for (i = 0; i < 10; i++) cprintf(" %p", pcs[i]);
    panicked = 1;  // freeze other CPU
    for (;;)
        ;
}

// PAGEBREAK: 50
#define NULLCHAR '\0'
#define BACKSPACE 0x100
#define LEFTARR 0x101
#define RIGHTARR 0x102
#define UPARR 0x103    // TO DO
#define DOWNARR 0x104  // TO DO
#define CRTPORT 0x3d4
static ushort *crt = (ushort *)P2V(0xb8000);  // CGA memory

static void cgaputc(int c) {
    int pos;

    // Cursor position: col + 80*row.
    outb(CRTPORT, 14);
    pos = inb(CRTPORT + 1) << 8;
    outb(CRTPORT, 15);
    pos |= inb(CRTPORT + 1);

    if (c == '\n')
        pos += 80 - pos % 80;
    else if (c == BACKSPACE || c == LEFTARR) {
        if (pos > 0) --pos;
    } else if (c == UPARR || c == DOWNARR) {
        ;
    } else if (c == RIGHTARR) {
        ++pos;
    } else
        crt[pos++] = (c & 0xff) | 0x0700;  // black on white

    if (pos < 0 || pos > 25 * 80) panic("pos under/overflow");

    if ((pos / 80) >= 24) {  // Scroll up.
        memmove(crt, crt + 80, sizeof(crt[0]) * 23 * 80);
        pos -= 80;
        memset(crt + pos, 0, sizeof(crt[0]) * (24 * 80 - pos));
    }

    outb(CRTPORT, 14);
    outb(CRTPORT + 1, pos >> 8);
    outb(CRTPORT, 15);
    outb(CRTPORT + 1, pos);
    if (c != LEFTARR && c != RIGHTARR) crt[pos] = ' ' | 0x0700;
}

void consputc(int c) {
    if (panicked) {
        cli();
        for (;;)
            ;
    }

    if (c == BACKSPACE) {
        uartputc('\b');
        uartputc(' ');
        uartputc('\b');
    } else
        uartputc(c);
    cgaputc(c);
}

#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    uint r;  // Read index, position to read from
    uint w;  // Write index, position till written + 1
    uint e;  // Edit index, the current position of cursor
} input;

#define C(x) ((x) - '@')  // Control-x

void consoleintr(int (*getc)(void)) {
    int c, doprocdump = 0;

    acquire(&cons.lock);
    while ((c = getc()) >= 0) {
        switch (c) {
            case C('P'):  // Process listing.
                // procdump() locks cons.lock indirectly; invoke later
                doprocdump = 1;
                break;
            case C('U'):  // Kill line.
                while (input.w != input.r &&
                       input.buf[(input.w - 1) % INPUT_BUF] != '\n') {
                    input.w--;
                    consputc(BACKSPACE);
                }
                input.e = input.w;
                break;
            case C('H'):
            case '\x7f':  // Backspace
                if (input.e != input.r && input.e > 0) {
                    // Shifting the input buffer one place left
                    for (uint i = input.e; i < input.w; ++i) {
                        input.buf[(i - 1 + INPUT_BUF) % INPUT_BUF] =
                            input.buf[i % INPUT_BUF];
                    }
                    // Moving cursor to the right
                    for (uint i = input.e; i < input.w; ++i) {
                        consputc(RIGHTARR);
                    }
                    // Clearing till the previous position of cursor
                    for (uint i = input.e - 1; i < input.w; ++i) {
                        consputc(BACKSPACE);
                    }
                    // Inserting the updated input buffer from changed position
                    for (uint i = input.e - 1; i + 1 < input.w; ++i) {
                        consputc(input.buf[i % INPUT_BUF]);
                    }
                    // Moving the cursor back to correct position
                    for (uint i = input.e; i < input.w; ++i) {
                        consputc(LEFTARR);
                    }
                    input.e--;
                    input.w--;
                }
                break;
            case 0xe4:  // LEFT ARROW
                // Shift cursor one place left
                if (input.e != input.r) {
                    input.e--;
                    consputc(LEFTARR);
                }
                break;
            case 0xe5:  // RIGHT ARROW
                // Shift cursor one place right
                if (input.e < input.w) {
                    input.e++;
                    consputc(RIGHTARR);
                }
                break;
            case 0xe2:  // UP ARROW
                // go one command up in the history buffer
                if (currCnt > 0) {
                    currCnt--;
                    // first move right
                    for (uint i = input.e; i < input.w; ++i) {
                        consputc(RIGHTARR);
                    }
                    // clear the input
                    for (uint i = input.r; i < input.w; ++i) {
                        consputc(BACKSPACE);
                    }
                    input.w = input.r;
                    // printing history character wise
                    char tmp;
                    for (int i = 0; i < strlen(history_buffer[currCnt]); i++) {
                        tmp = history_buffer[currCnt][i];
                        consputc(tmp);
                        input.buf[input.w++] = tmp;
                    }
                    // sync the inputs
                    input.e = input.w;
                }
                break;
            case 0xe3:  // DOWN ARROW
                // go one command down in the history buffer
                if (currCnt < histCnt - 1) {
                    // first move right
                    for (uint i = input.e; i < input.w; ++i) {
                        consputc(RIGHTARR);
                    }
                    // clear the input
                    for (uint i = input.r; i < input.w; ++i) {
                        consputc(BACKSPACE);
                    }

                    input.w = input.r;
                    // show last command
                    currCnt++;
                    char tmp;
                    for (int i = 0; i < strlen(history_buffer[currCnt]); i++) {
                        tmp = history_buffer[currCnt][i];
                        consputc(tmp);
                        input.buf[input.w++] = tmp;
                    }
                    // sync the inputs
                    input.e = input.w;
                }
                break;
            default:
                if (c != 0 && input.w - input.r < INPUT_BUF) {
                    c = (c == '\r') ? '\n' : c;
                    // If enter pressed, insert \n after input
                    if (c == '\n' || c == C('D')) {
                        input.buf[input.w % INPUT_BUF] = c;
                        for (uint i = input.e; i < input.w; ++i) {
                            consputc(RIGHTARR);
                        }
                        consputc(c);
                    } else {
                        // Shifting the input buffer one place right
                        for (uint i = input.w; i > input.e; --i) {
                            input.buf[i % INPUT_BUF] =
                                input.buf[(i - 1 + INPUT_BUF) % INPUT_BUF];
                        }
                        // inserting the new character
                        input.buf[input.e % INPUT_BUF] = c;
                        // Moving on-screen cursor to the right
                        for (uint i = input.e; i < input.w; ++i) {
                            consputc(RIGHTARR);
                        }
                        // Clearing characters till the previous position
                        for (uint i = input.e; i < input.w; ++i) {
                            consputc(BACKSPACE);
                        }
                        // Inserting the updated input buffer
                        for (int i = input.e; i <= input.w; ++i) {
                            consputc(input.buf[i % INPUT_BUF]);
                        }
                        // Moving cursor back to correct position
                        for (int i = input.e; i < input.w; ++i) {
                            consputc(LEFTARR);
                        }
                    }
                    input.w++;
                    input.e++;
                    // If enter pressed, prepare the buffer and wake consoleread()
                    if (c == '\n' || c == C('D') ||
                        input.w == input.r + INPUT_BUF) {
                         // Add the command to history
                        char rel_buffer[MAX_BUFFER_LEN];
                        for (int i = input.r, k = 0; i < input.w - 1;
                             i++, k++) {
                            rel_buffer[k] = input.buf[i % INPUT_BUF];
                        }
                        rel_buffer[(input.w - 1 - input.r) % INPUT_BUF] =
                            NULLCHAR;
                        add_to_history(rel_buffer);
                        
                        input.e = input.w;
                        // wake consoleread()
                        wakeup(&input.r);
                    }
                }
                break;
        }
    }
    release(&cons.lock);
    if (doprocdump) {
        procdump();  // now call procdump() wo. cons.lock held
    }
}

int consoleread(struct inode *ip, char *dst, int n) {
    uint target;
    int c;

    iunlock(ip);
    target = n;
    acquire(&cons.lock);
    while (n > 0) {
        while (input.r == input.w) {
            if (myproc()->killed) {
                release(&cons.lock);
                ilock(ip);
                return -1;
            }
            sleep(&input.r, &cons.lock);
        }
        c = input.buf[input.r++ % INPUT_BUF];
        if (c == C('D')) {  // EOF
            if (n < target) {
                // Save ^D for next time, to make sure
                // caller gets a 0-byte result.
                input.r--;
            }
            break;
        }
        *dst++ = c;
        --n;
        if (c == '\n') break;
    }
    release(&cons.lock);
    ilock(ip);

    return target - n;
}

int consolewrite(struct inode *ip, char *buf, int n) {
    int i;

    iunlock(ip);
    acquire(&cons.lock);
    for (i = 0; i < n; i++) consputc(buf[i] & 0xff);
    release(&cons.lock);
    ilock(ip);

    return n;
}

void consoleinit(void) {
    initlock(&cons.lock, "console");

    devsw[CONSOLE].write = consolewrite;
    devsw[CONSOLE].read = consoleread;
    cons.locking = 1;

    ioapicenable(IRQ_KBD, 0);
}

/*
Input:
  char * buffer ‐ a pointer to a buffer that will hold the history command,
  Assume max buffer size 128.
  historyId ‐ The history line requested, values 0 to 15
Output:
  0 ‐ History copied to the buffer properly
  1 ‐ No history for the given id
  2 ‐ historyId illegal
*/
int sys_history(void) {
    char *myBuffer;
    int id;

    // fetching parameters - namely Buffer and ID
    int fetchBufferIllegal = argstr(0, &myBuffer) < 0;
    int fetchIDIllegal = argint(1, &id) < 0;

    // checking whether arguments are fetched correctly
    if (fetchBufferIllegal || fetchIDIllegal) {
        return 1;
    }

    // checking whether the ID is positive and does not exceed pre defined
    // history limit
    int illegalHistory = ((id < 0) || (id >= MAX_HISTORY));
    if (illegalHistory) {
        return 2;
    }

    // checking whether we have requested history or not
    int noHistory = id >= histCnt;
    if (noHistory) {
        return 1;
    }

    // copying from history buffer to local buffer using memmove
    int unit_size = sizeof(char);
    memmove(myBuffer, history_buffer[id], MAX_BUFFER_LEN * unit_size);
    return 0;
}

void add_to_history(char *new_command) {
    int unit_size = sizeof(char);

    int null_command = (new_command[0] == NULLCHAR);

    // if it is a null command then it is not added to the buffer
    if (null_command) return;

    int actualLen = strlen(new_command);
    int length = actualLen;
    if (MAX_BUFFER_LEN - 1 < actualLen) {
        length = MAX_BUFFER_LEN - 1;
    }
    // will be used to check if history buffer has enough space
    int histLeft = histCnt < MAX_HISTORY;

    if (histLeft) {
        // can still accommodate new entry
        histCnt++;
    } else {
        // need to shift all entries
        for (int i = 0; i + 1 < MAX_HISTORY; i++) {
            memmove(history_buffer[i], history_buffer[i + 1],
                    unit_size * length);
        }
    }
    // adding new command to existing history buffer
    memmove(history_buffer[histCnt - 1], new_command, unit_size * length);
    history_buffer[histCnt - 1][length] = NULLCHAR;

    // updating the current counter value
    currCnt = histCnt;
    return;
}