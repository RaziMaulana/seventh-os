#ifndef SEVENTHOS_CMD_FS_H
#define SEVENTHOS_CMD_FS_H

#include <stdint.h>

const char *cmd_fs_cwd(void);

void cmd_pwd(void);
void cmd_cd(const char *args);
void cmd_ls(const char *args);
void cmd_mkdir(const char *args);
void cmd_touch(const char *args);
void cmd_cat(const char *args);
void cmd_write(const char *args);
void cmd_append(const char *args);
void cmd_rm(const char *args);
void cmd_rmdir(const char *args);
void cmd_stat(const char *args);
void cmd_df(void);
void cmd_fstest(void);
int fstest_run(uint32_t *pass_out, uint32_t *fail_out);
void cmd_fsfill(void);

#endif