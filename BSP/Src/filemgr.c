/**
 ******************************************************************************
 * @file    filemgr.c
 * @brief   统一的文件管理组件（目录浏览 + 复制/移动/删除）
 * @note    界面分三态：
 *            FM_BROWSE  上方预览 + 下方文件列表
 *            FM_ACTION  选中文件后的二级菜单（复制/移动/删除/取消）
 *            FM_DEST    复制/移动的目标目录选择（列表顶部两条固定项）
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "filemgr.h"
#include "lcd.h"
#include "key.h"
#include "storage.h"
#include "jpeg_view.h"

#define FM_PATH_MAX   96
#define FM_LIST_MAX   STORAGE_MAX_ENTRIES
#define FM_ROW_H      26
#define FM_ROWS       13

typedef enum
{
    FM_BROWSE = 0,
    FM_ACTION,
    FM_DEST
} fm_mode_t;

/* 布局 */
static u16 s_pw;                 /* 预览区宽 */
static u16 s_ph;                 /* 预览区高 */
static u16 s_sy;                 /* 文本区起始 y */

/* 设备层：同时插着 SD 卡和板载 Flash 时，根目录先列出两个设备 */
static storage_dev_t s_devlist[2];
static u32   s_devcnt;
static u8    s_devlevel;                 /* 1 = 正在显示设备列表 */

/* 目录状态 */
static char  s_path[FM_PATH_MAX];        /* 当前目录(相对卷根, ""=根) */
static storage_entry_t s_ent[FM_LIST_MAX];
static u32   s_cnt;
static u32   s_dir_cnt;                  /* 前 s_dir_cnt 项是目录 */
static u32   s_sel;
static u32   s_top;

/* 模式与操作 */
static fm_mode_t s_mode;
static u8    s_act;                      /* 二级菜单: 0复制 1移动 2删除 3取消 */
static u8    s_is_move;                  /* 目标选择时: 1=移动 */
static char  s_src[FM_PATH_MAX];         /* 源文件相对路径 */
static char  s_ret_path[FM_PATH_MAX];    /* 操作完成后返回的目录 */
static u32   s_ret_sel;
static char  s_msg[48];
static u8    s_exit;

/* 预览状态 */
static u8    s_clear_first;              /* 换了新文件：先把预览区清空，再开始加载 */
static u8    s_prev_pending;             /* 当前选中项的图还没画完，需要继续补画 */

/* ---------------- 基础工具 ---------------- */

static void fm_str(u16 x, u16 y, u8 size, const char *s, u32 fc, u32 bc)
{
    POINT_COLOR = fc;
    BACK_COLOR  = bc;
    LCD_ShowString(x, y, (u16)(lcddev.width - x), size, size, (const u8 *)s);
}

/* 拷贝字符串，超长时截断并保证以 0 结尾（等价于 snprintf 的安全版本，
 * 但不用格式化，避免 -O2 下的 -Wformat-truncation 误报） */
static void fm_copy(char *out, u32 n, const char *s)
{
    u32 i = 0;

    if ((out == 0) || (n == 0))
    {
        return;
    }
    while ((s[i] != 0) && ((i + 1) < n))
    {
        out[i] = s[i];
        i++;
    }
    out[i] = 0;
}

/* dir + name -> 相对路径 */
static void fm_join(const char *dir, const char *name, char *out, u32 n)
{
    u32 i = 0;

    if ((out == 0) || (n < 2))
    {
        return;
    }

    while ((dir[0] != 0) && (dir[i] != 0) && ((i + 1) < n))
    {
        out[i] = dir[i];
        i++;
    }
    if ((i + 1) < n)
    {
        out[i++] = '/';
    }
    {
        u32 k = 0;
        while ((name[k] != 0) && ((i + 1) < n))
        {
            out[i++] = name[k++];
        }
    }
    out[i] = 0;
}

static void fm_basename(const char *path, char *out, u32 n)
{
    const char *p = strrchr(path, '/');
    p = (p != 0) ? (p + 1) : path;
    fm_copy(out, n, p);
}

/* "/A/B" -> "/A"; "/A" -> "" */
static void fm_parent(char *path)
{
    char *p = strrchr(path, '/');
    if (p != 0)
    {
        *p = 0;
    }
}

/* 按总行数修正选中项与滚动窗口 */
static void fm_scroll_rows(u32 rows)
{
    if (rows == 0)
    {
        s_sel = 0;
        s_top = 0;
        return;
    }
    if (s_sel >= rows)
    {
        s_sel = rows - 1;
    }
    if (s_sel < s_top)
    {
        s_top = s_sel;
    }
    if (s_sel >= (s_top + FM_ROWS))
    {
        s_top = s_sel - FM_ROWS + 1;
    }
    if ((s_top + FM_ROWS) > rows)
    {
        s_top = (rows > FM_ROWS) ? (rows - FM_ROWS) : 0;
    }
}

static u32 fm_dest_rows(void)
{
    return 2U + s_dir_cnt;
}

/* 重新读取当前目录 */
static void fm_load(void)
{
    u32 i;

    if (s_devlevel)
    {
        s_cnt     = 0;
        s_dir_cnt = 0;
        return;
    }

    s_cnt = Storage_ListDir(s_path, s_ent, FM_LIST_MAX);

    s_dir_cnt = 0;
    for (i = 0; i < s_cnt; i++)
    {
        if (s_ent[i].is_dir)
        {
            s_dir_cnt++;
        }
        else
        {
            break;                       /* 目录都在前面 */
        }
    }

    if (s_mode == FM_DEST)
    {
        fm_scroll_rows(fm_dest_rows());
    }
    else
    {
        fm_scroll_rows(s_cnt);
    }
}

/* 换了一个新的选中项：预览区先清空，再重新加载这张图。
 * 注意：只有"换了文件"才调用这里；进二级菜单、在菜单里上下选择都不算换文件，
 * 不会打断正在进行的加载。 */
static void fm_preview_invalidate(void)
{
    s_clear_first  = 1;
    s_prev_pending = 1;
}

/* ---------------- 绘制 ---------------- */

static void fm_draw_list(void)
{
    char buf[128];
    u16  y0 = s_sy;
    u16  i;

    LCD_Fill(0, y0, (u16)(lcddev.width - 1), (u16)(lcddev.height - 1), BLACK);
    LCD_Fill(0, s_ph, (u16)(lcddev.width - 1), (u16)(s_ph + 1), GRAY);

    snprintf(buf, sizeof(buf), "%s  %s", Storage_DevName(), (s_path[0] != 0) ? s_path : "/");
    fm_str(6, (u16)(y0 + 4), 16, buf, CYAN, BLACK);

    if (s_cnt == 0)
    {
        fm_str(6, (u16)(y0 + 32), 16, "Empty", GRAY, BLACK);
    }
    else
    {
        for (i = 0; i < FM_ROWS; i++)
        {
            u32 idx = s_top + i;
            u16 y   = (u16)(y0 + 26 + i * FM_ROW_H);

            if (idx >= s_cnt)
            {
                break;
            }

            if (s_ent[idx].is_dir)
            {
                snprintf(buf, sizeof(buf), "%c [%s]", (idx == s_sel) ? '>' : ' ', s_ent[idx].name);
            }
            else
            {
                snprintf(buf, sizeof(buf), "%c %-12s %5luK", (idx == s_sel) ? '>' : ' ',
                         s_ent[idx].name, (unsigned long)s_ent[idx].size_kb);
            }

            if (idx == s_sel)
            {
                LCD_Fill(0, (u16)(y - 3), (u16)(lcddev.width - 1),
                         (u16)(y + FM_ROW_H - 6), DARKBLUE);
                fm_str(6, y, 16, buf, WHITE, DARKBLUE);
            }
            else
            {
                fm_str(6, y, 16, buf, LGRAY, BLACK);
            }
        }
    }

    if (s_msg[0] != 0)
    {
        fm_str(6, (u16)(lcddev.height - 42), 16, s_msg, YELLOW, BLACK);
    }
    fm_str(6, (u16)(lcddev.height - 20), 16, "UP/DN:Sel  RIGHT:Open  LEFT:Back", WHITE, BLACK);
}

/* 二级菜单：样式与文件列表完全一致（16 号字 / 26px 行高 / 深蓝高亮 / '>' 前缀）。
 * 进入前先把文本区清空，避免和下面的文件列表叠加成重影。 */
static void fm_draw_action(void)
{
    static const char *const items[4] = { "Copy", "Move", "Delete", "Cancel" };
    char buf[128];
    u16  y0;
    u8   i;

    if ((s_cnt == 0) || (s_sel >= s_cnt))
    {
        return;
    }

    LCD_Fill(0, s_sy, (u16)(lcddev.width - 1), (u16)(lcddev.height - 1), BLACK);
    LCD_Fill(0, s_ph, (u16)(lcddev.width - 1), (u16)(s_ph + 1), GRAY);

    snprintf(buf, sizeof(buf), "Action: %s", s_ent[s_sel].name);
    fm_str(6, (u16)(s_sy + 4), 16, buf, CYAN, BLACK);

    y0 = (u16)(s_sy + 26);

    for (i = 0; i < 4; i++)
    {
        u16 y = (u16)(y0 + i * FM_ROW_H);

        snprintf(buf, sizeof(buf), "%c %s", (i == s_act) ? '>' : ' ', items[i]);

        if (i == s_act)
        {
            LCD_Fill(0, (u16)(y - 3), (u16)(lcddev.width - 1),
                     (u16)(y + FM_ROW_H - 6), DARKBLUE);
            fm_str(6, y, 16, buf, WHITE, DARKBLUE);
        }
        else
        {
            fm_str(6, y, 16, buf, LGRAY, BLACK);
        }
    }

    fm_str(6, (u16)(lcddev.height - 20), 16, "UP/DN:Sel  RIGHT:OK  LEFT:Cancel", WHITE, BLACK);
}

/* 设备列表：同时插了 SD 卡和板载 Flash 时才出现 */
static void fm_draw_devices(void)
{
    char buf[128];
    u16  i;

    LCD_Fill(0, s_sy, (u16)(lcddev.width - 1), (u16)(lcddev.height - 1), BLACK);
    LCD_Fill(0, s_ph, (u16)(lcddev.width - 1), (u16)(s_ph + 1), GRAY);

    fm_str(6, (u16)(s_sy + 4), 16, "Storage", CYAN, BLACK);

    for (i = 0; i < s_devcnt; i++)
    {
        u16 y = (u16)(s_sy + 26 + i * FM_ROW_H);

        snprintf(buf, sizeof(buf), "%c %s", (i == s_sel) ? '>' : ' ',
                 (s_devlist[i] == STORAGE_SD) ? "[SD CARD]" : "[FLASH]");

        if (i == s_sel)
        {
            LCD_Fill(0, (u16)(y - 3), (u16)(lcddev.width - 1),
                     (u16)(y + FM_ROW_H - 6), DARKBLUE);
            fm_str(6, y, 16, buf, WHITE, DARKBLUE);
        }
        else
        {
            fm_str(6, y, 16, buf, LGRAY, BLACK);
        }
    }

    if (s_msg[0] != 0)
    {
        fm_str(6, (u16)(lcddev.height - 42), 16, s_msg, YELLOW, BLACK);
    }
    fm_str(6, (u16)(lcddev.height - 20), 16, "UP/DN:Sel  RIGHT:Open  LEFT:Exit", WHITE, BLACK);
}

static void fm_draw_dest(void)
{
    char buf[128];
    u16  y0 = s_sy;
    u16  i;
    u32  rows = fm_dest_rows();

    LCD_Fill(0, y0, (u16)(lcddev.width - 1), (u16)(lcddev.height - 1), BLACK);
    LCD_Fill(0, s_ph, (u16)(lcddev.width - 1), (u16)(s_ph + 1), GRAY);

    snprintf(buf, sizeof(buf), "%s to: %s", s_is_move ? "Move" : "Copy",
             (s_path[0] != 0) ? s_path : "/");
    fm_str(6, (u16)(y0 + 4), 16, buf, CYAN, BLACK);

    for (i = 0; i < FM_ROWS; i++)
    {
        u32 idx = s_top + i;
        u16 y   = (u16)(y0 + 26 + i * FM_ROW_H);

        if (idx >= rows)
        {
            break;
        }

        if (idx == 0)
        {
            snprintf(buf, sizeof(buf), "%c %s here", (idx == s_sel) ? '>' : ' ',
                     s_is_move ? "Move" : "Copy");
        }
        else if (idx == 1)
        {
            snprintf(buf, sizeof(buf), "%c Cancel", (idx == s_sel) ? '>' : ' ');
        }
        else
        {
            snprintf(buf, sizeof(buf), "%c [%s]", (idx == s_sel) ? '>' : ' ', s_ent[idx - 2].name);
        }

        if (idx == s_sel)
        {
            LCD_Fill(0, (u16)(y - 3), (u16)(lcddev.width - 1),
                     (u16)(y + FM_ROW_H - 6), DARKBLUE);
            fm_str(6, y, 16, buf, WHITE, DARKBLUE);
        }
        else
        {
            fm_str(6, y, 16, buf, LGRAY, BLACK);
        }
    }

    if (s_msg[0] != 0)
    {
        fm_str(6, (u16)(lcddev.height - 42), 16, s_msg, YELLOW, BLACK);
    }
    fm_str(6, (u16)(lcddev.height - 20), 16, "UP/DN:Sel  RIGHT:OK  LEFT:Up", WHITE, BLACK);
}

/* 画当前选中项的图像。
 * 只要 pending 还在，就一直重试直到画完（被按键打断时 pending 保持为 1，
 * 主循环空闲后会自动再调一次）。二级菜单/设备列表下也可以继续补画。 */
static void fm_draw_preview(void)
{
    char rel[FM_PATH_MAX];
    char abs[FM_PATH_MAX];
    u8   r;

    if (s_devlevel || (s_mode == FM_DEST))
    {
        return;                          /* 目标选择时 s_sel 指向的是目录列表，不能画 */
    }
    if (!s_prev_pending)
    {
        return;
    }

    /* 换到新文件：先把预览区整块清空，然后立刻开始加载新图 */
    if (s_clear_first)
    {
        LCD_Fill(0, 0, (u16)(s_pw - 1), (u16)(s_ph - 1), BLACK);
        s_clear_first = 0;
    }

    if ((s_cnt == 0) || (s_sel >= s_cnt))
    {
        s_prev_pending = 0;
        return;
    }
    if (s_ent[s_sel].is_dir)
    {
        s_prev_pending = 0;              /* 目录没有图，保持清空后的黑底 */
        return;
    }

    fm_join(s_path, s_ent[s_sel].name, rel, sizeof(rel));
    Storage_AbsPath(rel, abs, sizeof(abs));      /* 解码需要带卷前缀的绝对路径 */

    r = Jpeg_ShowFile(abs, 0, 0, s_pw, s_ph);

    if (r == 30)
    {
        return;                          /* 30=被按键打断：不清屏，稍后接着画完 */
    }

    s_prev_pending = 0;
}

/* 主循环空闲时调用：没有按键按着就把没画完的图补完。
 * 这样即使中途进了二级菜单，图也会一直加载到显示完整。 */
static void fm_service_preview(void)
{
    if (!s_prev_pending)
    {
        return;
    }
    if (s_devlevel || (s_mode == FM_DEST))
    {
        return;
    }
    if (KEY_AnyDown())
    {
        return;                          /* 按键优先，松手再画 */
    }

    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);
    fm_draw_preview();
}

static void fm_draw(void)
{
    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);

    if (s_devlevel)
    {
        fm_draw_devices();
        return;
    }

    /* 二级菜单 / 目标选择：只重画下方文本区，上方预览图保持不动，
     * 没画完的部分交给 fm_service_preview() 在空闲时补 */
    if (s_mode == FM_ACTION)
    {
        fm_draw_action();
        return;
    }
    if (s_mode == FM_DEST)
    {
        fm_draw_dest();
        return;
    }

    fm_draw_list();          /* 先刷文本（优先响应） */
    fm_draw_preview();       /* 再解码图像（可被按键打断） */
}

/* ---------------- 按键处理 ---------------- */

static key_dir_t fm_key(void)
{
    if (Jpeg_KeyEvent != 0)
    {
        key_dir_t k = (key_dir_t)Jpeg_KeyEvent;
        Jpeg_KeyEvent = 0;
        return k;
    }
    return KEY_GetDirEx(1);
}

/* 取消目标选择：什么都没改，预览保持原样，不重新加载 */
static void fm_dest_cancel(void)
{
    snprintf(s_path, sizeof(s_path), "%s", s_ret_path);
    s_mode = FM_BROWSE;
    s_sel  = s_ret_sel;
    fm_load();
}

static void fm_do_transfer(void)
{
    char name[STORAGE_ENTRY_NAME];
    char dst[FM_PATH_MAX];
    u8   r;

    fm_basename(s_src, name, sizeof(name));
    fm_join(s_path, name, dst, sizeof(dst));

    if (strcmp(dst, s_src) == 0)
    {
        snprintf(s_msg, sizeof(s_msg), "Same location");
        return;
    }

    r = s_is_move ? Storage_MoveFile(s_src, dst) : Storage_CopyFile(s_src, dst);

    if (r == 0)
    {
        snprintf(s_msg, sizeof(s_msg), "%s OK", s_is_move ? "Moved" : "Copied");
    }
    else if (r == 6)
    {
        snprintf(s_msg, sizeof(s_msg), "Target exists");
    }
    else
    {
        snprintf(s_msg, sizeof(s_msg), "%s failed (%u)", s_is_move ? "Move" : "Copy", (unsigned)r);
    }

    fm_dest_cancel();

    /* 移动成功后源目录少了一个文件，选中项实际变了，需要重新加载预览 */
    if ((r == 0) && s_is_move)
    {
        fm_preview_invalidate();
    }
}

/* 设备列表的按键处理 */
static void fm_key_dev(key_dir_t k)
{
    if (k == KEY_DIR_UP)
    {
        if (s_devcnt != 0)
        {
            s_sel = (s_sel + s_devcnt - 1) % s_devcnt;
        }
    }
    else if (k == KEY_DIR_DOWN)
    {
        if (s_devcnt != 0)
        {
            s_sel = (s_sel + 1) % s_devcnt;
        }
    }
    else if (k == KEY_DIR_LEFT)
    {
        s_exit = 1;
    }
    else if (k == KEY_DIR_RIGHT)
    {
        if ((s_devcnt != 0) && (Storage_SetDev(s_devlist[s_sel]) == 0))
        {
            s_devlevel = 0;
            s_path[0]  = 0;
            s_sel      = 0;
            s_top      = 0;
            s_msg[0]   = 0;
            fm_load();
            fm_preview_invalidate();
        }
        else
        {
            snprintf(s_msg, sizeof(s_msg), "Not available");
        }
    }
}

static void fm_key_browse(key_dir_t k)
{
    if (k == KEY_DIR_UP)
    {
        if (s_cnt != 0)
        {
            s_sel = (s_sel + s_cnt - 1) % s_cnt;
            fm_scroll_rows(s_cnt);
            fm_preview_invalidate();
        }
    }
    else if (k == KEY_DIR_DOWN)
    {
        if (s_cnt != 0)
        {
            s_sel = (s_sel + 1) % s_cnt;
            fm_scroll_rows(s_cnt);
            fm_preview_invalidate();
        }
    }
    else if (k == KEY_DIR_LEFT)
    {
        if (s_path[0] == 0)
        {
            if (s_devcnt >= 2)
            {
                s_devlevel = 1;              /* 回到设备列表 */
                s_sel      = 0;
                s_top      = 0;
                s_msg[0]   = 0;
            }
            else
            {
                s_exit = 1;                  /* 只有一个设备：根目录再返回=退出组件 */
            }
        }
        else
        {
            fm_parent(s_path);
            s_sel = 0;
            s_top = 0;
            fm_load();
            fm_preview_invalidate();
        }
    }
    else if (k == KEY_DIR_RIGHT)
    {
        if (s_cnt == 0)
        {
            return;
        }
        if (s_ent[s_sel].is_dir)
        {
            char np[FM_PATH_MAX];
            fm_join(s_path, s_ent[s_sel].name, np, sizeof(np));
            snprintf(s_path, sizeof(s_path), "%s", np);
            s_sel = 0;
            s_top = 0;
            fm_load();
            fm_preview_invalidate();
        }
        else
        {
            s_mode = FM_ACTION;
            s_act  = 0;
            s_msg[0] = 0;
        }
    }
}

static void fm_key_action(key_dir_t k)
{
    if (k == KEY_DIR_UP)
    {
        s_act = (u8)((s_act + 3) % 4);
    }
    else if (k == KEY_DIR_DOWN)
    {
        s_act = (u8)((s_act + 1) % 4);
    }
    else if (k == KEY_DIR_LEFT)
    {
        s_mode = FM_BROWSE;
    }
    else if (k == KEY_DIR_RIGHT)
    {
        if ((s_cnt == 0) || (s_sel >= s_cnt))
        {
            s_mode = FM_BROWSE;
            return;
        }

        if ((s_act == 0) || (s_act == 1))
        {
            /* 复制 / 移动：进入目标目录选择，记住原位置 */
            fm_join(s_path, s_ent[s_sel].name, s_src, sizeof(s_src));
            snprintf(s_ret_path, sizeof(s_ret_path), "%s", s_path);
            s_ret_sel = s_sel;
            s_is_move = (s_act == 1) ? 1 : 0;

            s_path[0] = 0;                       /* 从根开始选目标 */
            s_sel = 0;
            s_top = 0;
            s_msg[0] = 0;
            s_mode = FM_DEST;
            fm_load();
        }
        else if (s_act == 2)
        {
            char full[FM_PATH_MAX];
            fm_join(s_path, s_ent[s_sel].name, full, sizeof(full));
            if (Storage_DeletePath(full) == 0)
            {
                snprintf(s_msg, sizeof(s_msg), "Deleted");
            }
            else
            {
                snprintf(s_msg, sizeof(s_msg), "Delete failed");
            }
            s_mode = FM_BROWSE;
            fm_load();
            fm_preview_invalidate();
        }
        else
        {
            s_mode = FM_BROWSE;
        }
    }
}

static void fm_key_dest(key_dir_t k)
{
    u32 rows = fm_dest_rows();

    if (k == KEY_DIR_UP)
    {
        if (rows != 0)
        {
            s_sel = (s_sel + rows - 1) % rows;
            fm_scroll_rows(rows);
        }
    }
    else if (k == KEY_DIR_DOWN)
    {
        if (rows != 0)
        {
            s_sel = (s_sel + 1) % rows;
            fm_scroll_rows(rows);
        }
    }
    else if (k == KEY_DIR_LEFT)
    {
        if (s_path[0] == 0)
        {
            fm_dest_cancel();                    /* 已在根，左键=取消 */
        }
        else
        {
            fm_parent(s_path);
            s_sel = 0;
            s_top = 0;
            fm_load();
        }
    }
    else if (k == KEY_DIR_RIGHT)
    {
        if (s_sel == 0)
        {
            fm_do_transfer();                    /* 在此处复制/移动 */
        }
        else if (s_sel == 1)
        {
            fm_dest_cancel();                    /* 取消 */
        }
        else if ((s_sel - 2) < s_dir_cnt)
        {
            char np[FM_PATH_MAX];
            fm_join(s_path, s_ent[s_sel - 2].name, np, sizeof(np));
            snprintf(s_path, sizeof(s_path), "%s", np);
            s_sel = 0;
            s_top = 0;
            fm_load();
        }
    }
}

/* ---------------- 对外入口 ---------------- */

void FileMgr_Run(void)
{
    storage_dev_t dev0 = Storage_Dev();      /* 进入前的卷，退出时恢复 */

    s_pw = lcddev.width;
    s_ph = (u16)((lcddev.width * 3U / 4U) & 0xFFFCU);
    s_sy = (u16)(s_ph + 2);

    /* 探测可用设备：都插着才显示设备列表，否则直接进唯一那个 */
    s_devcnt = 0;
    if (Storage_HasFlash())
    {
        s_devlist[s_devcnt++] = STORAGE_FLASH;
    }
    if (Storage_HasSd())
    {
        s_devlist[s_devcnt++] = STORAGE_SD;
    }

    s_devlevel = (s_devcnt >= 2) ? 1 : 0;
    if (!s_devlevel && (s_devcnt == 1))
    {
        (void)Storage_SetDev(s_devlist[0]);
    }

    s_path[0] = 0;
    s_sel = 0;
    s_top = 0;
    s_mode = FM_BROWSE;
    s_act = 0;
    s_msg[0] = 0;
    s_exit = 0;
    s_clear_first  = 1;              /* 进组件先清空预览区再加载第一张 */
    s_prev_pending = 1;

    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);
    LCD_Clear(BLACK);

    fm_load();
    fm_draw();

    while (!s_exit)
    {
        key_dir_t k = fm_key();

        if (k == KEY_DIR_NONE)
        {
            fm_service_preview();            /* 空闲：把没画完的图补完 */
            continue;
        }

        if (s_devlevel)
        {
            fm_key_dev(k);
        }
        else
        {
            switch (s_mode)
            {
                case FM_BROWSE: fm_key_browse(k); break;
                case FM_ACTION: fm_key_action(k); break;
                case FM_DEST:   fm_key_dest(k);   break;
                default:        break;
            }
        }

        if (!s_exit)
        {
            fm_draw();
        }
    }

    (void)Storage_SetDev(dev0);              /* 恢复原卷，不影响拍照/录像 */

    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);
    LCD_Clear(BLACK);
}
