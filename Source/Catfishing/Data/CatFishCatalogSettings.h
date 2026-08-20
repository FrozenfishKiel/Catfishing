#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Data/CatDataCatalogTypes.h"
#include "Data/CatFishDefinition.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatFishCatalogSettings.generated.h"

class UCatEquipmentSettings;

/** 生成 Encounter 所需的纯数据输入；它不包含 PlayerController、Character 或运行时对象指针。 */
USTRUCT(BlueprintType)
struct FCatFishEncounterSelectionRequest
{
	GENERATED_BODY()

	/** Cast 时刻冻结的 WaterRegion 稳定 ID；None 会让选择 fail-closed。 */
	UPROPERTY(BlueprintReadWrite)
	FName RegionId = NAME_None;

	/** Cast 时刻冻结的局内时段。鱼表格没有按时段区分鱼种，所以它不参与候选过滤；保留 Unknown 拒绝是因为环境快照未裁决时这一竿本就不该开始。 */
	UPROPERTY(BlueprintReadWrite)
	ECatEnvironmentTimeOfDay TimeOfDay = ECatEnvironmentTimeOfDay::Unknown;

	/** Cast 时刻冻结的天气。同样不参与候选过滤，只作为"钓鱼必须发生在已裁决环境快照下"这条前置条件的一半。 */
	UPROPERTY(BlueprintReadWrite)
	ECatEnvironmentWeather Weather = ECatEnvironmentWeather::Unknown;

	/** Cast 时刻可参与搏斗的 authority 玩家数；范围固定为 1 到 8。 */
	UPROPERTY(BlueprintReadWrite)
	int32 ActivePlayerCount = 0;

	/** Cast 时刻合法参与者 FishingStrength 合计；鱼表只消费冻结快照，不重新读 ASC。 */
	UPROPERTY(BlueprintReadWrite)
	double CombinedFishingStrength = 0.0;

	/** Cast 时刻合法参与者 FightStamina 合计；鱼表只消费冻结快照，不重新读 ASC。
	 *  它不参与候选过滤——体力在飞书里是搏斗中逐秒扣减的资源，不是可钓性资格。这里仍要求它是正数并原样带进结果，
	 *  是因为 Fishing Session 会用它初始化搏斗快照里的猫方合计体力。 */
	UPROPERTY(BlueprintReadWrite)
	double CombinedFightStamina = 0.0;

	/** 服务器为本次 encounter 冻结的随机种子；相同目录、输入和种子必须生成同一结果。 */
	UPROPERTY(BlueprintReadWrite)
	int32 SelectionSeed = 0;
};

/** FishCatalog 生成的一次确定性 Encounter 数据结果；Fishing EncounterSpec 从这里复制数据事实。 */
USTRUCT(BlueprintType)
struct FCatFishEncounterSelectionResult
{
	GENERATED_BODY()

	/** 是否成功选择到一条可运行鱼定义；false 时其他业务字段保持默认。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelected = false;

	/** 目录 SchemaVersion；进入 EncounterSpec 后用于审计内容兼容性。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SchemaVersion = 1;

	/** 鱼目录 DataRevision；进入 EncounterSpec 后用于证明选择来自哪一版内容。 */
	UPROPERTY(BlueprintReadOnly)
	int64 DataRevision = 0;

	/** 鱼目录 ContentHash；同 Revision 但内容不同会生成不同摘要，避免旧结果被误认成同一内容。 */
	UPROPERTY(BlueprintReadOnly)
	FString ContentHashHex;

	/** 本次抽取使用的服务器种子；EncounterSpec 保存它后，同内容和输入可以复现同一鱼种与重量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SelectionSeed = 0;

	/** 被选择的 FishDefinition 稳定 ID；后续系统只用 ID 查询，不持久化资产指针。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 被选择鱼定义的体型协作档位；Session Snapshot 只消费这个冻结结论。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishBodyClass BodyClass = ECatFishBodyClass::Unknown;

	/** 本次确定性生成的鱼重；范围由 FishDefinition 明确给出。 */
	UPROPERTY(BlueprintReadOnly)
	double FishWeightKilograms = 0.0;

	/** 被选择鱼定义的搏斗体力；Session 初始化后从该值开始消耗。 */
	UPROPERTY(BlueprintReadOnly)
	double FishFightStamina = 0.0;

	/** 选择时使用的合法参与者人数；用于审计候选过滤结果。 */
	UPROPERTY(BlueprintReadOnly)
	int32 FightParticipantCount = 0;

	/** 选择时使用的合法参与者 FishingStrength 合计；用于审计候选过滤结果。 */
	UPROPERTY(BlueprintReadOnly)
	double CombinedFishingStrength = 0.0;

	/** 选择时使用的合法参与者 FightStamina 合计；它没有参与候选过滤，Session 用它初始化搏斗快照里的猫方合计体力。 */
	UPROPERTY(BlueprintReadOnly)
	double CombinedFightStamina = 0.0;
};

/** 正式鱼表资产目录；它是运行时鱼定义的唯一枚举入口，不从文件名、Content 扫描或飞书运行时读取猜内容。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Fish Catalog"))
class CATFISHING_API UCatFishCatalogSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 当前代码支持的鱼目录内容 SchemaVersion；导入或人工落盘改变结构时必须显式升级。 */
	static constexpr int32 CurrentContentSchemaVersion = 1;

	/**
	 * 按稳定 ID 查找完整且启用的鱼定义；重复 ID、加载失败或目录校验失败时返回空。
	 *
	 * 和装备目录一样，这里每次都重新校验而不缓存：本类是 DeveloperSettings，生产代码读它的 CDO，而自动化测试用
	 * "改 CDO 字段 → 断言 → 析构还原"的守卫直接写这些字段，那条写入不触发 PostEditChangeProperty，缓存无从失效。
	 */
	UCatFishDefinition* FindRuntimeDefinition(FName FishDefinitionId) const;

	/** 生成包含 SchemaVersion、DataRevision 和 ContentHash 的确定性鱼 Encounter 数据；失败时不选择 fallback 鱼。 */
	FCatFishEncounterSelectionResult GenerateEncounterSelection(const FCatFishEncounterSelectionRequest& Request,
		const UCatEquipmentSettings* EquipmentCatalog) const;

	/**
	 * 校验鱼目录整体是否可运行；除来源戳与单条 readiness 外，还逐元素检查地点、窝料归属和特殊饵 ID，并可选验证
	 * PreferredSpecialBaitIds 是否指向真实特殊饵。
	 *
	 * bComputeContentHash 决定要不要顺带算出 ContentHashHex。摘要只是内容身份戳，"目录能不能跑"只看 Issues 是否为空，
	 * 摘要不参与；但算它要把整表鱼定义解析、排序、拼成 canonical 文本再过一遍 Blake3。所以只有真的要读这个字段的
	 * 调用方（Encounter 生成、数据目录校验 commandlet）才让它为 true，纯查询路径传 false。
	 * OutRuntimeById 让调用方拿走本次校验已经解析出来的"稳定鱼种 ID → 运行定义"映射，省掉查询侧的第二遍线性扫描；
	 * 目录不可运行时交出空表，避免半合法内容绕过 fail-closed。
	 */
	FCatDataCatalogValidationResult ValidateRuntimeCatalog(const UCatEquipmentSettings* EquipmentCatalog = nullptr,
		bool bComputeContentHash = true,
		TMap<FName, UCatFishDefinition*>* OutRuntimeById = nullptr) const;

	/** 计算当前显式鱼目录的稳定内容摘要；摘要只来自目录字段，不扫描 Content 或外部文档。 */
	FString ComputeContentHashHex() const;

	/** 鱼目录 SchemaVersion；运行时代码只接受 CurrentContentSchemaVersion。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	int32 ContentSchemaVersion = CurrentContentSchemaVersion;

	/** 鱼目录数据修订；人工落盘或离线导入内容变化时递增，运行时不读取飞书修订。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	int64 DataRevision = 0;

	/** 鱼目录的来源戳；它让人工落盘和 CI 能追到权威资料，但运行时不会据此读取外部系统。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	FCatDataCatalogSourceStamp SourceStamp;

	/** 正式 FishDefinition 软引用清单；默认空使 Fishing fail-closed，不扫描旧鱼种文档或测试资产。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TArray<TSoftObjectPtr<UCatFishDefinition>> Definitions;
};
