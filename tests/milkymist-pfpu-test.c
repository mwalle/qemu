/*
 * QTest testcase for the Milkymist PFPU
 *
 * Copyright (c) 2018 Michael Walle <michael@walle.cc>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest.h"

#define PFPU_BASE 0x60006000
#define PFPU_IRQ 8

static void pfpu_load_microcode(uint32_t *insn, int len)
{
    uint32_t addr = PFPU_BASE + 0x800;
    while (len) {
        writel(addr, *insn);
        insn++;
        len--;
        addr += 4;
    }
}

static void pfpu_clear_microcode(void)
{
    uint32_t addr = PFPU_BASE + 0x800;
    uint32_t len = 512;
    while (len) {
        writel(addr, 0);
        len--;
        addr += 4;
    }
}

static uint32_t float_to_u32(float val)
{
    return (*((uint32_t *)&(val)));
}

static void pfpu_add(void)
{
    static uint32_t ucode[] = {
        0x000c2080, /* FADD R3, R4 */
        0x00000000, /* NOP */
        0x00000000, /* NOP */
        0x00000000, /* NOP */
        0x00000000, /* NOP */
        0x00000003, /* NOP | EXIT R3 */
        0x000c2380  /* VECTOUT R3, R4 */
    };

    /* 2x1 mesh size */
    writel(PFPU_BASE + 0x8, 1);
    writel(PFPU_BASE + 0xc, 0);

    /* write test operands to r3 and r4 */
    writel(PFPU_BASE + 0x40c, float_to_u32(3.0f));
    writel(PFPU_BASE + 0x410, float_to_u32(9.0f));

    pfpu_load_microcode(ucode, ARRAY_SIZE(ucode));

    /* dma base */
    writel(PFPU_BASE + 0x4, 0x40000000);

    /* start */
    writel(PFPU_BASE, 1);

    /* on a successful run, the busy flag should be cleared */
    g_assert(readl(PFPU_BASE) == 0);

    /* interrupt line should have been pulsed */
    g_assert(get_irq_latched(PFPU_IRQ) == 1);
    clear_irq_latch(PFPU_IRQ);
    g_assert(get_irq_latched(PFPU_IRQ) == 0);

    /* resulting vertices should be written to RAM */
    g_assert(readl(0x40000000) == float_to_u32(12.0f));
    g_assert(readl(0x40000004) == float_to_u32(9.0f));
    g_assert(readl(0x40000008) == float_to_u32(21.0f));
    g_assert(readl(0x4000000c) == float_to_u32(9.0f));

    /* count of computed vertices */
    g_assert(readl(PFPU_BASE + 0x14) == 2);

    /* no collisions */
    g_assert(readl(PFPU_BASE + 0x18) == 0);

    /* no stray writes */
    g_assert(readl(PFPU_BASE + 0x1c) == 0);
}

static void pfpu_microcode_overflow(void)
{
    /* 1x1 mesh size */
    writel(PFPU_BASE + 0x8, 1);
    writel(PFPU_BASE + 0xc, 0);

    pfpu_clear_microcode();

    /* start */
    writel(PFPU_BASE, 1);

    /* because there is no VECTOUT, the busy flag should not be cleared */
    g_assert(readl(PFPU_BASE) == 1);

    /* and there should be no pending interrupt */
    g_assert(get_irq_latched(PFPU_IRQ) == 0);
}

static void pfpu_stray_writes(void)
{
    static uint32_t ucode[] = {
        0x000c0600, /* COPY R3 */
        0x00000000, /* NOP */
        0x00000000, /* NOP */
        0x000c2380  /* VECTOUT R3, R4 */
    };

    /* 1x1 mesh size */
    writel(PFPU_BASE + 0x8, 0);
    writel(PFPU_BASE + 0xc, 0);

    /* write test operands to r3 and r4 */
    writel(PFPU_BASE + 0x40c, float_to_u32(1.0f));
    writel(PFPU_BASE + 0x410, float_to_u32(2.0f));

    pfpu_load_microcode(ucode, ARRAY_SIZE(ucode));

    /* dma base */
    writel(PFPU_BASE + 0x4, 0x40000000);

    /* start */
    writel(PFPU_BASE, 1);
    clear_irq_latch(PFPU_IRQ);

    /* stray writes */
    g_assert(readl(PFPU_BASE + 0x1c) == 1);
}

static void pfpu_collision(void)
{
    static uint32_t ucode[] = {
        0x000c0300, /* I2F R3 */
        0x000c0600, /* COPY R3 */
        0x00000000, /* NOP */
        0x00000000, /* NOP */
        0x000c2380  /* VECTOUT R3, R4 */
    };

    /* 1x1 mesh size */
    writel(PFPU_BASE + 0x8, 0);
    writel(PFPU_BASE + 0xc, 0);

    /* write test operands to r3 and r4 */
    writel(PFPU_BASE + 0x40c, float_to_u32(1.0f));
    writel(PFPU_BASE + 0x410, float_to_u32(2.0f));

    pfpu_load_microcode(ucode, ARRAY_SIZE(ucode));

    /* dma base */
    writel(PFPU_BASE + 0x4, 0x40000000);

    /* start */
    writel(PFPU_BASE, 1);
    clear_irq_latch(PFPU_IRQ);

    /* collisions */
    g_assert(readl(PFPU_BASE + 0x18) == 1);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_start("-machine milkymist");
    irq_intercept_in("lm32-pic");
    qtest_add_func("/milkymist-pfpu/add", pfpu_add);
    qtest_add_func("/milkymist-pfpu/microcode-overflow", pfpu_microcode_overflow);
    qtest_add_func("/milkymist-pfpu/collision", pfpu_collision);
    qtest_add_func("/milkymist-pfpu/stray-writes", pfpu_stray_writes);

    ret = g_test_run();

    qtest_end();

    return ret;
}
