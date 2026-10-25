/*
 * large_struct_offsets.c - regression for ADDI-immediate truncation.
 *
 * Bug (fixed 2026-10-25, found while bringing `obrowse` up live): struct
 * member offsets and stack-slot displacements were emitted with a raw
 * `addi` whose 12-bit signed immediate silently masked the value, so any
 * offset that did not fit in [-2048, 2047] wrapped:
 *
 *     char a[4096];  int x;   ->  &x - &s  ==  0        (should be 4096)
 *     char a[16384]; int y;   ->  &y - &s  ==  4        (should be 16388)
 *
 * obrowse's `struct obrowse_page` (16 KiB text + 64 x 256-byte links) hit
 * exactly this: `page->links[0]` resolved to `page + 8`, the parser wrote
 * hrefs over `text_len`, and the browser rendered an empty page with a
 * garbage link count (`links: 1701273969` == "page").
 *
 * The fix routes every offset-bearing add through rv_addi_imm (lui+addi
 * widening), the same treatment loads/stores already had.
 *
 * Returns 0 when every offset and access lands where C says it should;
 * otherwise a bitmask of failures (bit N = check N failed).
 */
typedef unsigned long usize;

struct link { char href[256]; };

struct big4096 { char a[4096]; int x; };
struct big8192 { char a[8192]; int x; };
struct big16k  { char a[16384]; int x; };
struct two8k   { char a[8192]; char b[8192]; int x; };
struct prefix  { int x; char a[16384]; int y; };
struct page {
    char text[16384];
    usize text_len;
    struct link links[64];
    int link_count;
    char url[256];
};

static struct big4096 g4;
static struct big8192 g8;
static struct big16k g16;
static struct two8k g2;
static struct prefix gp;
static struct page gpage;

int _start(void) {
    int fail = 0;

    if ((char *)&g4.x - (char *)&g4 != 4096) fail |= 1 << 0;
    if ((char *)&g8.x - (char *)&g8 != 8192) fail |= 1 << 1;
    if ((char *)&g16.x - (char *)&g16 != 16384) fail |= 1 << 2;
    if ((char *)&g2.x - (char *)&g2 != 16384) fail |= 1 << 3;
    if ((char *)&gp.y - (char *)&gp != 16388) fail |= 1 << 4;

    if ((char *)&gpage.links[0] - (char *)&gpage != 16392) fail |= 1 << 5;
    if ((char *)&gpage.links[1] - (char *)&gpage != 16392 + 256) fail |= 1 << 6;
    if ((char *)&gpage.link_count - (char *)&gpage != 32776) fail |= 1 << 7;
    if ((char *)&gpage.url - (char *)&gpage != 32780) fail |= 1 << 8;

    /* Store through a large offset and read it back: catches an emitter
     * that gets the address right for `&` but wrong for the access. */
    g16.x = 12345;
    gpage.link_count = 3;
    gpage.links[2].href[0] = 'Z';
    gpage.links[2].href[1] = 0;
    gpage.text[16000] = 'q';
    if (g16.x != 12345) fail |= 1 << 9;
    if (gpage.link_count != 3) fail |= 1 << 10;
    if (gpage.links[2].href[0] != 'Z') fail |= 1 << 11;
    if (gpage.text[16000] != 'q') fail |= 1 << 12;
    if (gpage.text[0] != 0) fail |= 1 << 13;  /* no aliasing back to the base */

    return fail;
}
