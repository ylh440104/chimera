typedef unsigned long chm_size_t;

extern long write(int fd, const void *buf, chm_size_t n);

static const char banner[] = "chimera: hello from arm64 Mach-O\n";
static const char tail[] = "chimera: main is alive\n";

int main(int argc, char **argv, char **envp, char **apple)
{
    int i;

    write(1, banner, sizeof(banner) - 1);
    for (i = 0; i < argc; i++) {
        chm_size_t n = 0;

        while (argv[i][n])
            n++;
        write(1, "  arg ", 6);
        write(1, argv[i], n);
        write(1, "\n", 1);
    }
    if (envp)
        write(1, "  envp ok\n", 10);
    if (apple)
        write(1, "  apple ok\n", 11);
    write(1, tail, sizeof(tail) - 1);
    return 0;
}