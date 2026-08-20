#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Collection/CatImprintSettings.h"

namespace CatImprintSettingsTest
{
	/**
	 * 临时改写印记准入配置的测试守卫。
	 *
	 * 印记候选准入读取 GetDefault<UCatImprintSettings>，而项目默认是 fail-closed 的空触发清单加 0 上限：
	 * 触发名单没裁完之前，任何候选都会被判 PolicyUndecided。所以除了专门验证 fail-closed 的用例，
	 * 凡是要走通"候选被接受"这条路的用例都必须先把本轮需要的事件名和上限显式打开，用完恢复。
	 *
	 * 它放在共享头文件而不是各测试文件里各写一份，是因为这里真正容易出错的是析构还原：
	 * 少还原一项就会把改过的配置漏给同批次后面的用例，而那种污染表现为"另一个用例莫名其妙地过了或挂了"，
	 * 极难定位。同一份还原逻辑只应该存在一处。
	 */
	struct FImprintSettingsOverride
	{
		/** 被临时改写的默认配置对象；测试不写磁盘配置，只改内存默认对象。 */
		UCatImprintSettings* Settings = GetMutableDefault<UCatImprintSettings>();

		/** 原始触发总清单；析构时原样写回。 */
		TArray<FName> OldAllowedEventIds;

		/** 原始单局候选上限；析构时原样写回。 */
		int32 OldMaxRunImprintCandidates = 0;

		// 构造流程：先保存项目默认值，再写入调用方指定的触发清单与单局上限。
		explicit FImprintSettingsOverride(const TArray<FName>& InAllowedEventIds, const int32 InMaxRunImprintCandidates)
		{
			if (Settings)
			{
				OldAllowedEventIds = Settings->AllowedImprintEventIds;
				OldMaxRunImprintCandidates = Settings->MaxRunImprintCandidates;
				Settings->AllowedImprintEventIds = InAllowedEventIds;
				Settings->MaxRunImprintCandidates = InMaxRunImprintCandidates;
			}
		}

		// 析构流程：只还原内存默认对象，不触碰磁盘配置。
		~FImprintSettingsOverride()
		{
			if (Settings)
			{
				Settings->AllowedImprintEventIds = OldAllowedEventIds;
				Settings->MaxRunImprintCandidates = OldMaxRunImprintCandidates;
			}
		}
	};
}

#endif
