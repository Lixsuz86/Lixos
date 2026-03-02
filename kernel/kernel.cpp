extern "C" void kernel_main();

volatile unsigned short* video = (volatile unsigned short*)0xB8000;
int cursor_pos = 0;

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static unsigned char kbd_wait_scancode() {
    while (!(inb(0x64) & 1)) {}
    return inb(0x60);
}

static const char scancode_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']', '\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};

static void scroll() {
    for (int i = 0; i < 80 * 24; i++) video[i] = video[i + 80];
    for (int i = 80 * 24; i < 80 * 25; i++) video[i] = (unsigned short)' ' | 0x0700;
    cursor_pos = 80 * 24;
}

static void put_char(char c) {
    if (c == '\n') {
        cursor_pos = (cursor_pos / 80 + 1) * 80;
    } else if (c == '\b') {
        if (cursor_pos > 0) {
            cursor_pos--;
            video[cursor_pos] = (unsigned short)' ' | 0x0700;
        }
    } else {
        video[cursor_pos++] = (unsigned short)c | 0x0700;
    }
    if (cursor_pos >= 80 * 25) scroll();
}

static void print(const char* s) {
    while (*s) put_char(*s++);
}

static int kstrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void kernel_main() {
    print("LIXOS Yuklendi!\nKomut bekliyor: ");
    char buf[64];
    int len = 0;
    
    while (1) {
        unsigned char sc = kbd_wait_scancode();
        if (sc & 0x80) continue;
        char c = scancode_map[sc];
        if (!c) continue;

        if (c == '\n') {
            put_char('\n');
            buf[len] = 0;
            if (kstrcmp(buf, "help") == 0) print("Komutlar: help, clear, version\n");
            else if (kstrcmp(buf, "clear") == 0) {
                for (int i = 0; i < 80 * 25; i++) video[i] = 0x0700;
                cursor_pos = 0;
            }
            else if (kstrcmp(buf, "version") == 0) print("LIXOS v1.0 Alpha\n");
            else if (len > 0) print("Bilinmeyen komut.\n");
            
            print("> ");
            len = 0;
        } else if (c == '\b' && len > 0) {
            put_char('\b');
            len--;
        } else if (len < 63 && c != '\b') {
            put_char(c);
            buf[len++] = c;
        }
    }
}