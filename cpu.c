/*
 * Flag info per instruction:
 * http://www.chrisantonellis.com/files/gameboy/gb-instructions.txt
 *
 * BIOS explanation:
 * https://realboyemulator.wordpress.com/2013/01/03/a-look-at-the-game-boy-bootstrap-let-the-fun-begin/
 */

#include "cpu.h"
#include <string.h>
#include <stdio.h>

#include "pause.h"
#include "mmu.h"

static void cpu_handle_interrupts(struct gb_state *state);
static void cpu_handle_LCD(struct gb_state *state, int op_cycles);

static const int GB_FREQ = 4194304; // Hz
static const int GB_LCD_WIDTH = 160;
static const int GB_LCD_HEIGHT = 144;

static const int GB_LCD_LY_MAX = 153;
static const int GB_LCD_WX_MAX = 166;
static const int GB_LCD_WY_MAX = 143;

static const int GB_LCD_MODE_0_CLKS = 204;
static const int GB_LCD_MODE_1_CLKS = 4560;
static const int GB_LCD_MODE_2_CLKS = 80;
static const int GB_LCD_MODE_3_CLKS = 172;

static const u8 flagmasks[] = { FLAG_Z, FLAG_Z, FLAG_C, FLAG_C };

static int cycles_per_instruction[] = {
  // 0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f
     4, 12,  8,  8,  4,  4,  8,  4, 20,  8,  8,  8,  4,  4,  8,  4, // 0
     4, 12,  8,  8,  4,  4,  8,  4, 12,  8,  8,  8,  4,  4,  8,  4, // 1
     8, 12,  8,  8,  4,  4,  8,  4,  8,  8,  8,  8,  4,  4,  8,  4, // 2
     8, 12,  8,  8, 12, 12, 12,  4,  8,  8,  8,  8,  4,  4,  8,  4, // 3
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 4
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 5
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 6
     8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4, // 7
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 8
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // 9
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4, // a
     4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  8,  4,  8,  4, // b
     8, 12, 12, 16, 12, 16,  8, 16,  8, 16, 12,  0, 12, 24,  8, 16, // c
     8, 12, 12,  4, 12, 16,  8, 16,  8, 16, 12,  4, 12,  4,  8, 16, // d
    12, 12,  8,  4,  4, 16,  8, 16, 16,  4, 16,  4,  4,  4,  8, 16, // e
    12, 12,  8,  4,  4, 16,  8, 16, 12,  8, 16,  4,  0,  4,  8, 16, // f
};

static int cycles_per_instruction_cb[] = {
  // 0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 0
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 1
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 2
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 3
     8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8, // 4
     8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8, // 5
     8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8, // 6
     8,  8,  8,  8,  8,  8, 12,  8,  8,  8,  8,  8,  8,  8, 12,  8, // 7
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 8
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // 9
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // a
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // b
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // c
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // d
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // e
     8,  8,  8,  8,  8,  8, 16,  8,  8,  8,  8,  8,  8,  8, 16,  8, // f
};

void cpu_reset_state(struct gb_state* s)
{
    s->freq = GB_FREQ;
    s->cycles = 0;

    s->reg16.AF = 0x01B0;
    s->reg16.BC = 0x0013;
    s->reg16.DE = 0x00D8;
    s->reg16.HL = 0x014D;

    s->sp = 0xFFFE;
    s->pc = 0x0100;

    if (1 || s->rom_type == 1) {
        s->reg16.AF = 0x1180;
        s->reg16.BC = 0x0000;
        s->reg16.DE = 0xff56;
        s->reg16.HL = 0x000d;
    }

    s->halt_for_interrupts = 0;
    s->interrupts_master_enabled = 1;
    s->interrupts_enable  = 0x0;
    s->interrupts_request = 0x0;

    s->lcd_mode_clks_left = 0;

    s->io_lcd_SCX  = 0x00;
    s->io_lcd_SCY  = 0x00;
    s->io_lcd_WX   = 0x00;
    s->io_lcd_WY   = 0x00;
    s->io_lcd_BGP  = 0xfc;
    s->io_lcd_OBP0 = 0xff;
    s->io_lcd_OBP1 = 0xff;
    s->io_lcd_BGPI = 0x00;
    s->io_lcd_BGPD = 0x00;
    s->io_lcd_OBPI = 0x00;
    s->io_lcd_OBPD = 0x00;
    s->io_lcd_LCDC = 0x91;
    s->io_lcd_LY   = 0x00;
    s->io_lcd_LYC  = 0x00;

    s->io_timer_DIV  = 0x00;
    s->io_timer_TIMA = 0x00;
    s->io_timer_TMA  = 0x00;
    s->io_timer_TAC  = 0x00;

    s->io_serial_data    = 0x00;
    s->io_serial_control = 0x00;

    s->io_infrared = 0x00;

    s->io_buttons = 0x00;
    s->io_buttons_dirs = 0x0f;
    s->io_buttons_buttons = 0x0f;


    s->io_sound_enabled = 0xf1;
    s->io_sound_out_terminal = 0xf3;
    s->io_sound_terminal_control = 0x77;

    s->io_sound_channel1_sweep = 0x80;
    s->io_sound_channel1_length_pattern = 0xbf;
    s->io_sound_channel1_envelope = 0xf3;
    s->io_sound_channel1_freq_lo = 0x00;
    s->io_sound_channel1_freq_hi = 0xbf;

    s->io_sound_channel2_length_pattern = 0x3f;
    s->io_sound_channel2_envelope = 0x00;
    s->io_sound_channel2_freq_lo = 0x00;
    s->io_sound_channel2_freq_hi = 0xbf;

    s->io_sound_channel3_enabled = 0x7f;
    s->io_sound_channel3_length = 0xff;
    s->io_sound_channel3_level = 0x9f;
    s->io_sound_channel3_freq_lo = 0x00;
    s->io_sound_channel3_freq_hi = 0xbf;
    memset(s->io_sound_channel3_ram, 0, 0xf);

    s->io_sound_channel4_length = 0xff;
    s->io_sound_channel4_envelope = 0x00;
    s->io_sound_channel4_poly = 0x00;
    s->io_sound_channel4_consec_initial = 0xbf;


    s->mem_bank_rom = 1;
    s->mem_bank_wram = 1;
    s->mem_bank_vram = 0;

    s->mem_ram_rtc_select = 0;

    memset(s->mem_RAM, 0, 4 * 0x2000);
    memset(s->mem_HRAM, 0, 0x7f);
    memset(s->mem_WRAM, 0, 8 * 0x1000);
    memset(s->mem_VRAM, 0, 2 * 0x2000);

    s->mem_latch_rtc = 0x01;
    memset(s->mem_RTC, 0, 0x05);

    s->in_bios = 1;
    s->pc = 0;

}

void cpu_print_regs(struct gb_state *s) {
    printf("\n\tAF\tBC\tDE\tHL\tSP\tPC\t\tLY\tZNHC\n");
    printf("\t%04x\t%04x\t%04x\t%04x\t%04x\t%04x\t\t%04x\t%d%d%d%d\n",
            s->reg16.AF, s->reg16.BC, s->reg16.DE, s->reg16.HL, s->sp, s->pc,
            s->io_lcd_LY, s->flags.ZF, s->flags.NF, s->flags.HF, s->flags.CF);
    printf("\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\n",
            s->reg8.A, s->reg8.F,
            s->reg8.B, s->reg8.C,
            s->reg8.D, s->reg8.E,
            s->reg8.H, s->reg8.L);
    /*
    printf("\tZ:%d\tN:%d\tH:%d\tC:%d\t\n\n", s->flags.Z, s->flags.N, s->flags.H, s->flags.C);
    */
}

static void cpu_handle_interrupts(struct gb_state *s) {
    // Does NOT check for interupts enabled master.
    u8 interrupts = s->interrupts_enable & s->interrupts_request;

    printf("Executing interrupt %d.\n", interrupts);

    for (int i = 0; i < 5; i++) {
        if (interrupts & (1 << i)) {
            s->interrupts_master_enabled = 0;
            s->interrupts_request ^= 1 << i;

            mmu_write(s, --(s->sp), (s->pc & 0xff00) >> 8);
            mmu_write(s, --(s->sp),  s->pc & 0x00ff);

            s->pc = i * 0x8 + 0x40;

            s->halt_for_interrupts = 0;
            return;
        }
    }
}

static void cpu_handle_LCD(struct gb_state *s, int op_cycles) {
    // The LCD goes through several states.
    // 0 = HBlank, 1 = VBlank, 2 = reading OAM, 3 = reading OAM and VRAM
    // 2 and 3 are between each HBlank
    // So the cycle goes like: 2330002330002330002330001111..1111233000...
    //                         OBBHHHOBBHHHOBBHHHOBBHHHVVVV..VVVVOBBHHH...
    // The entire cycle takes 70224 clks. (so that's about 60FPS)
    // HBlank takes about 201-207 cycles. VBlank 4560 clks.
    // 2 takes about 77-83 and 3 about 169-175 clks.

    s->lcd_mode_clks_left -= op_cycles;

    if (s->lcd_mode_clks_left < 0) {
        switch (s->io_lcd_STAT & 3) {
        case 0: // HBlank
            if (s->io_lcd_LY == 143) { // Go into VBlank (1)
                s->io_lcd_STAT = (s->io_lcd_STAT & 0xfc) | 1;
                s->lcd_mode_clks_left = GB_LCD_MODE_1_CLKS;
                s->interrupts_request |= 1 << 0;
            } else { // Back into OAM (2)
                s->io_lcd_STAT = (s->io_lcd_STAT & 0xfc) | 2;
                s->lcd_mode_clks_left = GB_LCD_MODE_2_CLKS;
            }
            s->io_lcd_LY = (s->io_lcd_LY + 1) % (GB_LCD_LY_MAX + 1);
            s->io_lcd_STAT = (s->io_lcd_STAT & 0xfb) | (s->io_lcd_LY == s->io_lcd_LYC);
            break;
        case 1: // VBlank, Back to OAM (2)
            s->io_lcd_STAT = (s->io_lcd_STAT & 0xfc) | 2;
            s->lcd_mode_clks_left = GB_LCD_MODE_2_CLKS;
            break;
        case 2: // OAM, onto OAM+VRAM (3)
            s->io_lcd_STAT = (s->io_lcd_STAT & 0xfc) | 3;
            s->lcd_mode_clks_left = GB_LCD_MODE_3_CLKS;
            break;
        case 3: // OAM+VRAM, let's HBlank (0)
            s->io_lcd_STAT = (s->io_lcd_STAT & 0xfc) | 0;
            s->lcd_mode_clks_left = GB_LCD_MODE_0_CLKS;
            break;
        }
    }

    if (s->io_lcd_STAT & (1 << 6) && s->io_lcd_STAT & (1 << 2)) // LY=LYC inter
        s->interrupts_request |= 1 << 1;

    if (s->io_lcd_STAT & (1 << 5) && (s->io_lcd_STAT & 3) == 2) // Mode 2 inter
        s->interrupts_request |= 1 << 1;

    if (s->io_lcd_STAT & (1 << 4) && (s->io_lcd_STAT & 3) == 1) // Mode 1 inter
        s->interrupts_request |= 1 << 1;

    if (s->io_lcd_STAT & (1 << 3) && (s->io_lcd_STAT & 3) == 0) // Mode 0 inter
        s->interrupts_request |= 1 << 1;
}

#define CF s->flags.CF
#define HF s->flags.HF
#define NF s->flags.NF
#define ZF s->flags.ZF
#define A s->reg8.A
#define F s->reg8.F
#define B s->reg8.B
#define C s->reg8.C
#define D s->reg8.D
#define E s->reg8.E
#define H s->reg8.H
#define L s->reg8.L
#define AF s->reg16.AF
#define BC s->reg16.BC
#define DE s->reg16.DE
#define HL s->reg16.HL
#define PC (s->pc)
#define M(op, value, mask) (((op) & (mask)) == (value))
#define mem(loc) (mmu_read(s, loc))
#define IMM8  (mmu_read(s, s->pc))
#define IMM16 (mmu_read(s, s->pc) | (mmu_read(s, s->pc + 1) << 8))
#define REG8(bitpos) s->reg8_lut[(op >> bitpos) & 7]
#define REG16(bitpos) s->reg16_lut[((op >> bitpos) & 3)]
#define REG16S(bitpos) s->reg16s_lut[((op >> bitpos) & 3)]
#define FLAG(bitpos) ((op >> bitpos) & 3)

static int do_cb_instruction(struct gb_state *s) {
    u8 op = mmu_read(s, s->pc++);

#if 0
    if (M(op, 0x00, 0xf8)) { // RLC reg8
    else if (M(op, 0x08, 0xf8)) { // RRC reg8
#endif
    if (M(op, 0x10, 0xf8)) { // RL reg8
        u8 *reg = REG8(0);
        u8 res = *reg << 1 | (CF ? 1 : 0);
        NF = 0;
        HF = 0;
        CF = (*reg) & (1 << 7);
        *reg = res;
        ZF = res == 0;
#if 0
    else if (M(op, 0x18, 0xf8)) { // RR reg8
    else if (M(op, 0x20, 0xf8)) { // SLA reg8
    else if (M(op, 0x28, 0xf8)) { // SRA reg8
    else if (M(op, 0x30, 0xf8)) { // SWAP reg8
    else if (M(op, 0x38, 0xf8)) { // SRL reg8
#endif
    } else if (M(op, 0x40, 0xc0)) { // BIT bit, reg8
        u8 bit = (op >> 3) & 7;
        u8 *reg = REG8(0);
        u8 res = *reg & (1 << bit);
        ZF = res == 0;
        NF = 0;
        HF = 1;
#if 0
    else if (M(op, 0x80, 0xc0)) { // RES bit, reg8
    else if (M(op, 0xc0, 0xc0)) { // RRC bit, reg8
#endif
    } else {
        s->pc -= 2;
        return 1;
    }
    return 0;
}

int cpu_do_instruction(struct gb_state *s) {
    // TODO:
    // * timer

    u8 op;
    //u8 temp1, temp2;
    //s8 stemp;
    //u16 ltemp;
    int op_cycles;

    if (s->interrupts_master_enabled && (s->interrupts_enable & s->interrupts_request)) {
        printf("handle interrupts\n");
        cpu_handle_interrupts(s);
        return 0; // temp?
    }

    op = mmu_read(s, s->pc++);
    op_cycles = cycles_per_instruction[op];
    if (op == 0xcb)
        op_cycles = cycles_per_instruction_cb[mmu_read(s, s->pc)];

    cpu_handle_LCD(s, op_cycles);
    s->cycles += op_cycles;

    if (s->halt_for_interrupts) {
        printf("halt interrupts\n");
        if (!s->interrupts_master_enabled || !s->interrupts_enable) {
            printf("Waiting for interrupts while disabled... Deadlock.\n");
            return 1;
        }
        s->pc--;
        return 0;
    }

    if (M(op, 0x00, 0xff)) { // NOP
    } else if (M(op, 0x01, 0xcf)) { /* LD reg16, u16 */
        u16 *dst = REG16(4);
        *dst = IMM16;
        s->pc += 2;
#if 0
    } else if (M(op, 0x02, 0xff)) { /* LD (BC), A */
    } else if (M(op, 0x03, 0xcf)) { /* INC reg16*/
#endif
    } else if (M(op, 0x04, 0xc7)) { /* INC reg8 */
        u8* reg = REG8(3);
        (*reg)++;
        NF = 0;
        ZF = *reg == 0;
        HF = (*reg & 0x10) == 0x10;
    } else if (M(op, 0x05, 0xc7)) { /* DEC reg8 */
        u8* reg = REG8(3);
        (*reg)--;
        NF = 1;
        ZF = *reg == 0;
        HF = (*reg & 0x0F) == 0x0F;
    } else if (M(op, 0x06, 0xc7)) { /* LD reg8, imm8 */
        u8* dst = REG8(3);
        u8 src = IMM8;
        s->pc++;
        if (dst)
            *dst = src;
        else
            mmu_write(s, HL, src);
#if 0
    } else if (M(op, 0x07, 0xff)) { /* RCLA */
    } else if (M(op, 0x08, 0xff)) { /* LD (imm16), SP */
    } else if (M(op, 0x09, 0xcf)) { /* ADD HL, reg16 */
    } else if (M(op, 0x0a, 0xff)) { /* LD A, (BC)*/
    } else if (M(op, 0x0b, 0xcf)) { /* DEC reg16 */
    } else if (M(op, 0x0f, 0xff)) { /* RRCA */
    } else if (M(op, 0x10, 0xff)) { /* STOP */
    } else if (M(op, 0x12, 0xff)) { /* LD (DE), A */
#endif
    } else if (M(op, 0x17, 0xff)) { /* RLA */
        u8 res = A << 1 | (CF ? 1 : 0);
        F = (A & (1 << 7)) ? FLAG_C : 0;
        A = res;
#if 0
    } else if (M(op, 0x18, 0xff)) { /* JR off8 */
#endif
    } else if (M(op, 0x1a, 0xff)) { /* LD A, (DE) */
        A = mem(DE);
#if 0
    } else if (M(op, 0x1f, 0xff)) { /* RRA */
#endif
    } else if (M(op, 0x20, 0xe7)) { /* JR cond, off8*/
        u8 flag = (op >> 3) & 3;
        if (((F & flagmasks[flag]) ? 1 : 0) == (flag & 1))
            s->pc += (s8)IMM8;
        s->pc++;
#if 0
    } else if (M(op, 0x22, 0xff)) { /* LDI (HL), A*/
    } else if (M(op, 0x27, 0xff)) { /* DAA */
    } else if (M(op, 0x2a, 0xff)) { /* LDI A, (HL) */
    } else if (M(op, 0x2f, 0xff)) { /* CPL */
#endif
    } else if (M(op, 0x32, 0xff)) { /* LDD (HL), A*/
        mmu_write(s, HL, A);
        HL--;
#if 0
    } else if (M(op, 0x37, 0xff)) { /* SCF */
    } else if (M(op, 0x3a, 0xff)) { /* LDD A, (HL) */
    } else if (M(op, 0x3f, 0xff)) { /* CCF */
#endif
    } else if (M(op, 0x40, 0xc0)) { /* LD reg8, reg8 */
        u8* src = REG8(0);
        u8* dst = REG8(3);
        u8 srcval = src ? *src : mem(HL);
        if (dst)
            *dst = srcval;
        else
            mmu_write(s, HL, srcval);
#if 0
    } else if (M(op, 0x76, 0xff)) { /* HALT */
#endif
    } else if (M(op, 0x80, 0xf8)) { /* ADD A, reg8 */
        u8* src = REG8(0);
        u8 srcval = src ? *src : mem(HL);
        u16 res = A + srcval;
        ZF = A == 0;
        NF = 0;
        HF = (A ^ srcval ^ res) & 0x10 ? 1 : 0;
        CF = res & 0x100 ? 1 : 0;
        A = (u8)res;
#if 0
    } else if (M(op, 0x88, 0xf8)) { /* ADC A, reg8 */
    } else if (M(op, 0x90, 0xf8)) { /* SUB reg8 */
    } else if (M(op, 0x98, 0xf8)) { /* SBC A, reg8*/
    } else if (M(op, 0xa0, 0xf8)) { /* AND reg8 */
#endif
    } else if (M(op, 0xa8, 0xf8)) { /* XOR reg8 */
        u8* src = REG8(0);
        u8 srcval = src ? *src : mem(HL);
        A ^= srcval;
        F = A ? 0 : FLAG_Z;
#if 0
    } else if (M(op, 0xb0, 0xf8)) { /* OR reg8 */
    } else if (M(op, 0xb8, 0xf8)) { /* CP reg8 */
    } else if (M(op, 0xc0, 0xe7)) { /* RET cond*/
#endif
    } else if (M(op, 0xc1, 0xcf)) { /* POP reg16*/
        u16 *dst = REG16S(4);
        u8 b1 = mem(s->sp++);
        u8 b2 = mem(s->sp++);
        *dst = (b1 << 8) | b2;
#if 0
    } else if (M(op, 0xc2, 0xe7)) { /* JP cond, imm16*/
#endif
    } else if (M(op, 0xc3, 0xff)) { /* JP imm16 */
        PC = IMM16;
#if 0
    } else if (M(op, 0xc4, 0xe7)) { /* CALL cond, imm16*/
#endif
    } else if (M(op, 0xc5, 0xcf)) { /* PUSH reg16*/
        u16 *src = REG16S(4);
        mmu_write(s, --(s->sp), *src & 0xff);
        mmu_write(s, --(s->sp), (*src & 0xff00) >> 8);
#if 0
    } else if (M(op, 0xc6, 0xff)) { /* ADD A, imm8*/
    } else if (M(op, 0xc7, 0xc7)) { /* RST imm8 */
    } else if (M(op, 0xc9, 0xff)) { /* RET */
#endif
    } else if (M(op, 0xcd, 0xff)) { /* CALL imm16 */
        u16 dst = IMM16;
        s->pc += 2;
        mmu_write(s, --(s->sp), s->pc & 0xff);
        mmu_write(s, --(s->sp), (s->pc & 0xff00) >> 8);
        s->pc = dst;
#if 0
    } else if (M(op, 0xce, 0xff)) { /* ADC imm8 */
    } else if (M(op, 0xd6, 0xff)) { /* SUB imm8 */
    } else if (M(op, 0xd9, 0xff)) { /* RETI */
    } else if (M(op, 0xde, 0xff)) { /* SBC imm8 */
#endif
    } else if (M(op, 0xe0, 0xff)) { /* LD (0xff00 + imm8), A*/
        mmu_write(s, 0xff00 + IMM8, A);
        s->pc++;
    } else if (M(op, 0xe2, 0xff)) { /* LD (0xff00 + C), A */
        mmu_write(s, 0xff00 + C, A);
#if 0
    } else if (M(op, 0xe6, 0xff)) { /* AND imm8 */
    } else if (M(op, 0xe8, 0xff)) { /* ADD SP, imm8s */
    } else if (M(op, 0xe9, 0xff)) { /* LD PC, HL */
#endif
    } else if (M(op, 0xea, 0xff)) { /* LD (imm16), A */
        mmu_write(s, IMM16, A);
        s->pc += 2;
    } else if (M(op, 0xcb, 0xff)) { /* CB-prefixed extended instructions */
        return do_cb_instruction(s);
#if 0
    } else if (M(op, 0xee, 0xff)) { /* XOR imm8 */
#endif
    } else if (M(op, 0xf0, 0xff)) { /* LD A, (0xff00 + imm8) */
        A = mmu_read(s, 0xff00 + IMM8);
        s->pc++;
#if 0
    } else if (M(op, 0xf2, 0xff)) { /* LD A, (0xff00 + C) */
#endif
    } else if (M(op, 0xf3, 0xff)) { /* DI */
        s->interrupts_master_enabled = 0;
#if 0
    } else if (M(op, 0xf6, 0xff)) { /* OR imm8 */
    } else if (M(op, 0xf8, 0xff)) { /* LD HL, SP + imm8 */
    } else if (M(op, 0xf9, 0xff)) { /* LD SP, HL */
#endif
    } else if (M(op, 0xfa, 0xff)) { /* LD A, (imm16) */
        A = mmu_read(s, IMM16);
        s->pc += 2;
#if 0
    } else if (M(op, 0xfb, 0xff)) { /* EI */
    } else if (M(op, 0xfe, 0xff)) { /* CP imm8 */
#endif
    } else {
        s->pc--;
        return 1;
    }

    return 0;
}

