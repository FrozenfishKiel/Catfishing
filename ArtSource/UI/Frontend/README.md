# 主菜单背景原稿

`T_UI_Frontend_LakeNight.png` 是前端的无字背景，导入到 `/Game/UI/Texture/Frontend/T_UI_Frontend_LakeNight`，由 `/Game/UI/Frontend/WBP_CatFrontendRoot` 的 `StaticBackgroundImage` 硬引用。背景独立 ScaleToFill，窄屏靠右裁剪保留猫和篝火；页面独立 ScaleToFit。按钮、标题、反馈和退出确认由 WBP 实时绘制。

来源：`Knowledge/Design/GDD 系统分册/ui/主界面.md` 首图，对应 `Knowledge/Design/_assets/VPRUbGAEwoFIFvxMbL2cxYfynMe.jpg`。2026-09-14 使用内置 imagegen 编辑工具移除参考图的文字和按钮，保留原参考文件。生成图对场景存在重绘，并非像素级无损去字。

导入及样式入口：在 UE Editor Python 中执行 `Scripts/style_frontend_menu.py`。重复执行复用已导入的贴图；若主动修改本原稿，需在编辑器中 Reimport 对应 Texture 后重新检查画面。

生成时的完整提示词：

> Use case: precise-object-edit. Edit target: the provided game main menu reference image. Create the clean background plate for an actual Unreal Engine game menu, landscape 16:9. Remove ALL text, title/logo, English subtitle, four menu labels, their markers and the translucent green button rectangle from the LEFT of the picture. Seamlessly reconstruct the underlying dark forest and lake scenery. Preserve the original scene composition, camera, moon, mountains, lake, wooden boat, dock, campfire and the small cat at lower right, teal moonlight and warm firelight. Keep the left third dark and calm for real UI widgets to be overlaid later. No new objects, no UI, no letters, no watermarks. The only change should be removing the UI and reconstructing the scene beneath it. Save a high resolution 16:9 background plate.
