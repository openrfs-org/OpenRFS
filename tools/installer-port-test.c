/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>

#include <rsd/install.h>
#include <rsd/line.h>

static int expect_step(const struct rsd_install *installer,
                       enum rsd_step expected, const char *name)
{
    if (installer->step == expected) {
        return 0;
    }
    fprintf(stderr, "%s: got %s\n", name, rsd_install_step_name(installer));
    return 1;
}

int main(void)
{
    struct rsd_install installer;

    rsd_install_begin(&installer);
    if (expect_step(&installer, RSD_STEP_KEYMAP, "entry") != 0) return 1;
    rsd_install_key(&installer, 0x1b);
    if (expect_step(&installer, RSD_STEP_WELCOME, "keymap escape") != 0) return 1;
    rsd_install_key(&installer, '\n');
    if (expect_step(&installer, RSD_STEP_HOSTNAME, "install selection") != 0) return 1;
    rsd_install_type(&installer, "-test");
    rsd_install_key(&installer, '\n');
    if (expect_step(&installer, RSD_STEP_COMPONENTS, "hostname") != 0) return 1;
    rsd_install_key(&installer, '\n');
    if (expect_step(&installer, RSD_STEP_METHOD, "components") != 0) return 1;
    rsd_install_key(&installer, '\n');
    if (expect_step(&installer, RSD_STEP_DISK, "method") != 0) return 1;
    rsd_install_key(&installer, '\n');
    if (expect_step(&installer, RSD_STEP_SCHEME, "disk") != 0) return 1;
    rsd_install_key(&installer, '\n');
    if (expect_step(&installer, RSD_STEP_REVIEW, "scheme") != 0) return 1;
    rsd_install_key(&installer, '\n');
    if (expect_step(&installer, RSD_STEP_COMMIT, "review") != 0) return 1;

    /* The destructive-looking screen must default to Back. */
    rsd_install_key(&installer, '\n');
    if (expect_step(&installer, RSD_STEP_REVIEW, "safe default") != 0) return 1;

    /* Continue runs only the in-memory plan and reaches later configuration. */
    rsd_install_key(&installer, '\n');
    rsd_install_key(&installer, RSD_KEY_LEFT);
    rsd_install_key(&installer, '\n');
    if (expect_step(&installer, RSD_STEP_WRITE, "preview planning") != 0) return 1;
    rsd_install_settle(&installer);
    if (expect_step(&installer, RSD_STEP_VERIFY, "preview validation") != 0) return 1;
    rsd_install_settle(&installer);
    if (expect_step(&installer, RSD_STEP_ROOTPW, "post-plan configuration") != 0) return 1;

    puts("installer port state and safe preview boundary passed");
    return 0;
}
