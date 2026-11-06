#define CON_PRINTBUFFER 1
#define CON_ENABLED 4
struct console {
    char name[8];
    void (*write)(struct console *, const char *, unsigned int);
    short flags, index;
};
extern void register_console(struct console *console);
