/**
 ******************************************************************************
 * @file    filemgr.h
 * @brief   统一的文件管理组件：目录浏览 / 复制 / 移动 / 删除
 * @note    FileMgr_Run() 是阻塞式界面，返回后由调用方继续主流程。
 *          操作方式：
 *            UP/DOWN  选择（长按连发）
 *            RIGHT    进入目录 / 打开文件二级菜单
 *            LEFT     返回上一层（根目录再按则退出组件）
 ******************************************************************************
 */
#ifndef __FILEMGR_H
#define __FILEMGR_H

#ifdef __cplusplus
extern "C" {
#endif

void FileMgr_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* __FILEMGR_H */
