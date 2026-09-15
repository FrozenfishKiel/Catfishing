# 前端背景原稿

`T_UI_Frontend_LakeNight.png` 是前端的无字背景，导入到 `/Game/UI/Texture/Frontend/T_UI_Frontend_LakeNight`，由 `/Game/UI/Frontend/WBP_CatFrontendRoot` 的 `StaticBackgroundImage` 硬引用。背景独立 ScaleToFill，窄屏靠右裁剪保留猫和篝火；页面独立 ScaleToFit。按钮、标题、反馈和退出确认由 WBP 实时绘制。

来源：`Knowledge/Design/GDD 系统分册/ui/主界面.md` 首图，对应 `Knowledge/Design/_assets/VPRUbGAEwoFIFvxMbL2cxYfynMe.jpg`。2026-09-14 使用内置 imagegen 编辑工具移除参考图的文字和按钮，保留原参考文件。生成图对场景存在重绘，并非像素级无损去字。

导入及样式入口：在 UE Editor Python 中执行 `Scripts/style_frontend_menu.py`。重复执行复用已导入的贴图；若主动修改本原稿，需在编辑器中 Reimport 对应 Texture 后重新检查画面。

生成时的完整提示词：

> Use case: precise-object-edit. Edit target: the provided game main menu reference image. Create the clean background plate for an actual Unreal Engine game menu, landscape 16:9. Remove ALL text, title/logo, English subtitle, four menu labels, their markers and the translucent green button rectangle from the LEFT of the picture. Seamlessly reconstruct the underlying dark forest and lake scenery. Preserve the original scene composition, camera, moon, mountains, lake, wooden boat, dock, campfire and the small cat at lower right, teal moonlight and warm firelight. Keep the left third dark and calm for real UI widgets to be overlaid later. No new objects, no UI, no letters, no watermarks. The only change should be removing the UI and reconstructing the scene beneath it. Save a high resolution 16:9 background plate.

## 房间营地背景（2026-09-15）

`T_UI_Frontend_CampNight.png` 导入为 `/Game/UI/Texture/Frontend/T_UI_Frontend_CampNight`，由房间 WBP 的隐藏 `RoomBackgroundSource` 硬引用。Root 在房间页将它放到原有全屏背景槽，离开房间页恢复 LakeNight。源文件与参考图均保留；不将按钮、姓名、头像或猫咪烘焙进背景。

来源为同一设计文档中的 `Knowledge/Design/_assets/FKueba2nQoEdyyxfdbDcIhjnneg.png`。使用内置 imagegen 的编辑模式生成，是场景重绘，不是原图的无损去字。工具返回文件为 `C:/Users/Administrator/.codex/generated_images/01a09e50-9108-7782-a8c6-2843a9b8632f/exec-e94912b7-ed2d-4935-af9e-a4cbd084d593.png`，工程原稿与其内容一致。

生成提示摘要：编辑参考营地为干净的 16:9 游戏大厅背景；去除全部 UI、文字、猫和背包，保留雪山、湖泊、月亮、帐篷、篝火、吊锅与灯笼；篝火周围留四个低木座，约位于画面宽度 34%、48%、63%、77%，脚部区域约在高度 78%；左侧四分之一留出安静暗色区域供实时 UI 使用；不要动物、文字或水印。

导入入口 `Scripts/style_frontend_room.py::camp_texture`；布局入口通过 `style_frontend_room_dialogs.py` 调用 `style_frontend_room_scene.py`。贴图使用 UI 纹理组、sRGB、无 mip、NeverStream。重复执行复用已导入资源；原稿有意更换时通过编辑器 Reimport 后检查。角色仍由现有 CuteCat 正面 idle 实时捕获，不属于本背景资产。
