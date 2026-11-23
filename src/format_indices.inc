enum { max_format_index = 128 };

static int slot_index(const char **format)
{
    const char *p = *format;
    if (*p < '1' || *p > '9') return 0;
    unsigned value = 0;
    do {
        if (value <= max_format_index) value = value * 10 + (unsigned)(*p - '0');
        p++;
    } while (*p >= '0' && *p <= '9');
    if (*p != '$') return 0;
    *format = p + 1;
    return value <= max_format_index ? (int)value : -1;
}

static int note_slot(int index, int type, unsigned char *types, int *highest, int *sequential)
{
    if (!index) { *sequential = 1; return 1; }
    if (index < 0 || (types[index] && types[index] != type)) return 0;
    types[index] = (unsigned char)type;
    if (index > *highest) *highest = index;
    return 1;
}

static int format_indices(const char *p)
{
    if (!strchr(p, '$')) return 0;
    unsigned char types[max_format_index + 1] = {0};
    int highest = 0, sequential = 0;
    for (; *p; ) {
        if (*p++ != '%') continue;
        if (*p == '%') { p++; continue; }
        int index = slot_index(&p);
        while (*p && strchr("-+ #0", *p)) p++;
        for (int field = 0; field < 2; field++) {
            if (field && *p != '.') break;
            if (field) p++;
            if (*p == '*') {
                p++;
                if (!note_slot(slot_index(&p), 'i', types, &highest, &sequential)) return -1;
            } else while (*p >= '0' && *p <= '9') p++;
        }
        int wide = *p && strchr("lqztjL", *p) != NULL;
        while (*p && strchr("hlqztjL", *p)) p++;
        if (!*p) return highest || index ? -1 : 0;
        int c = *p++;
        if (strchr("diuoxXc", c)) c = wide ? 'I' : 'i';
        else if (strchr("fFeEgGaA", c)) c = 'D';
        else if (!strchr("@sp", c)) { if (highest || index) return -1; sequential = 1; continue; }
        if (!note_slot(index, c, types, &highest, &sequential)) return -1;
    }
    if (highest && sequential) return -1;
    for (int i = 1; i <= highest; i++) if (!types[i]) return -1;
    return highest;
}