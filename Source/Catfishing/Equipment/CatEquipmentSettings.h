#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Data/CatDataCatalogTypes.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatEquipmentSettings.generated.h"

class UCatEquipmentDefinition;

/** 装备目录与失败预算调参；默认空目录/零损耗使装备命令 fail-closed。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Equipment"))
class CATFISHING_API UCatEquipmentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 当前代码支持的装备目录内容 SchemaVersion；导入或人工落盘改变结构时必须显式升级。 */
	static constexpr int32 CurrentContentSchemaVersion = 1;

	/**
	 * 按稳定 ID 查唯一完整定义；目录版本、重复 ID、缺失或非法定义都会让查询 fail-closed。
	 *
	 * 这条路径没有缓存，每次调用都重新校验一遍目录，这是刻意的：本类是 DeveloperSettings，生产代码读的是它的 CDO，
	 * 而自动化测试普遍用"改 CDO 字段 → 跑断言 → 在析构里改回来"的覆盖守卫直接写这些字段。那种写法不经过
	 * PostEditChangeProperty，也没有任何别的通知，缓存无法知道自己该失效，只会拿旧目录回答新配置的问题。
	 * 真正要摘掉的是这条路径上不必要的工作（内容摘要、重复加载、重复线性扫描），不是重算本身。
	 */
	UCatEquipmentDefinition* FindRuntimeDefinition(FName DefinitionId) const;

	/**
	 * 校验装备目录整体是否可运行；同时要求来源戳完整，运行时不得选择性忽略重复 ID 或半配置定义。
	 *
	 * bComputeContentHash 决定要不要顺带算出 ContentHashHex。内容摘要只是给校验报告用的内容身份戳，"目录能不能跑"
	 * 完全由 Issues 是否为空决定，摘要一个字节也不参与那个判断；但算一次摘要要把整表定义解析、排序、逐字段拼成
	 * canonical 文本，再整体过一遍 Blake3，比校验本身还贵。所以只有真正要读这个字段的调用方（数据目录校验
	 * commandlet 那条离线路径）才让它为 true，运行时查询路径一律传 false。
	 * OutRuntimeById 让调用方直接拿走本次校验解析出来的"稳定 ID → 运行定义"映射。校验循环本来就要把每条软引用
	 * 解析一遍，把结果带出去，按 ID 查定义就不必再扫一遍同一批引用。目录不可运行时这张表会被清空：非法目录里可能
	 * 有重复 ID 互相覆盖或半配置定义，让它带着残缺内容出去等于给 fail-closed 开后门。
	 */
	FCatDataCatalogValidationResult ValidateRuntimeCatalog(bool bComputeContentHash = true,
		TMap<FName, UCatEquipmentDefinition*>* OutRuntimeById = nullptr) const;

	/** 计算当前显式装备目录的稳定内容摘要；摘要只来自目录字段，不扫描 Content 或外部文档。 */
	FString ComputeContentHashHex() const;

	/** 读取项目配置的入局初始三件套；三项必须同时存在，缺一项时清空输出并让调用方 fail-closed。 */
	bool TryGetStarterLoadout(FName& OutRodDefinitionId, FName& OutBaitDefinitionId,
		FName& OutFloatDefinitionId) const;

	/** 装备目录 SchemaVersion；运行时代码只接受 CurrentContentSchemaVersion。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	int32 ContentSchemaVersion = CurrentContentSchemaVersion;

	/** 装备目录数据修订；人工落盘或离线导入内容变化时递增，运行时不读取飞书修订。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	int64 DataRevision = 0;

	/** 装备目录的来源戳；它让人工落盘和 CI 能追到权威资料，但运行时不会据此读取外部系统。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	FCatDataCatalogSourceStamp SourceStamp;

	/** 正式装备/道具清单；默认空且不扫描资产目录。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<TSoftObjectPtr<UCatEquipmentDefinition>> Definitions;

	/** 是否启用服务器装配入口的可信证明校验流程；Enabled 仍必须逐项通过 PlayerState 授权，本地 Profile 选择本身永不提升权限。 */
	UPROPERTY(Config, EditAnywhere, Category = "Loadout")
	ECatDomainPolicy ProfileLoadoutTrustPolicy = ECatDomainPolicy::Unset;

	/** 实际入局玩家的默认鱼竿定义 ID；只作为 starter 请求输入，最终仍要通过运行目录和 PlayerState 授权。 */
	UPROPERTY(Config, EditAnywhere, Category = "Loadout")
	FName StarterRodDefinitionId = NAME_None;

	/** 实际入局玩家的默认鱼饵定义 ID；普通免费饵和特殊饵语义由对应 Definition 决定。 */
	UPROPERTY(Config, EditAnywhere, Category = "Loadout")
	FName StarterBaitDefinitionId = NAME_None;

	/** 实际入局玩家的默认鱼漂定义 ID；配置缺失时 Character 不会自动生成替代漂。 */
	UPROPERTY(Config, EditAnywhere, Category = "Loadout")
	FName StarterFloatDefinitionId = NAME_None;

	/**
	 * 每种一局耗材在一只猫身上最多能带多少份（按定义 ID 分栈计数）。
	 * 飞书装备册已裁"普通饵/窝料各可带 5 份"并标注"暂定"，所以它是一个随身携带上限，而不是团队装备库容量。
	 * 0 表示不设上限；正值时 Equipment 的耗材授予入口在超出上限时返回 CapacityExceeded，不会把多出来的份数塞进栈里。
	 * 它只约束"领进来"，不约束消耗；预留/消耗/释放三条路径不读它。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Consumables", meta = (ClampMin = "0"))
	int32 RunConsumableStackCapacity = 0;

	/** 旧 DamageRod 失败预算耐久值；WORK-04 起生产路径不再读取它扣竿，保留用于兼容旧配置 Hash。 */
	UPROPERTY(Config, EditAnywhere, Category = "FailureBudget", meta = (ClampMin = "0.0"))
	double RodFailureDurabilityLoss = 0.0;

	/** 修竿点消费的浮木定义 ID；None 表示维修链未配置。 */
	UPROPERTY(Config, EditAnywhere, Category = "Repair")
	FName DriftwoodDefinitionId = NAME_None;
};
