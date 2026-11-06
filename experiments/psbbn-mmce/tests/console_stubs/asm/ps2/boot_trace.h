extern int psbbn_trace_row(const char *text, unsigned int row);
static inline void psbbn_trace(const char *text) { (void)psbbn_trace_row(text, 160); }
