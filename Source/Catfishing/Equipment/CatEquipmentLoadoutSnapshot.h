#pragma once

#include "CoreMinimal.h"
#include "CatEquipmentLoadoutSnapshot.generated.h"

/** Character 当前钓鱼选择的复制读模型；真实物品实例由 InventoryComponent 承载。 */
USTRUCT(BlueprintType)
struct FCatEquipmentLoadoutSnapshot
{
	GENERATED_BODY()

	/** 钓具选择或当前鱼竿摘要变化后递增；前端和钓鱼命令用它拒绝失效请求。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 当前选中鱼竿的数字物品编号，0 表示未选择；权威装备组件写入并复制，UI 与钓鱼读取，实际持有由对应库存实例证明。 */
	UPROPERTY(BlueprintReadOnly)
	int32  RodItemId = 0;

	/** 旧英文物品身份，仅供旧资产和旧档案单向迁移读取；新运行逻辑不读写，转换后清空。 */
	UPROPERTY()
	FName RodDefinitionId = NAME_None;


	/** 当前选中的鱼竿实例 ID；放杆时 Use 会按它移出库存，鱼竿 Actor 也复制它来阻止同实例重复出现。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RodItemInstanceId;

	/** 当前选中鱼饵的数字物品编号，0 表示未选择；权威装备组件写入并复制，UI 与钓鱼读取，可用数量以对应库存堆栈为准。 */
	UPROPERTY(BlueprintReadOnly)
	int32  BaitItemId = 0;

	/** 旧英文物品身份，仅供旧资产和旧档案单向迁移读取；新运行逻辑不读写，转换后清空。 */
	UPROPERTY()
	FName BaitDefinitionId = NAME_None;


	/** 当前选中的鱼饵堆栈实例 ID；Fishing 提交扣饵时按它锁定具体数量栈，不会再误扣同定义的另一格。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid BaitItemInstanceId;

	/** 当前选中浮漂的数字物品编号，0 表示未选择；权威装备组件写入并复制，UI 与钓鱼读取，不代表独立装备栏或实例身份。 */
	UPROPERTY(BlueprintReadOnly)
	int32  FloatItemId = 0;

	/** 旧英文物品身份，仅供旧资产和旧档案单向迁移读取；新运行逻辑不读写，转换后清空。 */
	UPROPERTY()
	FName FloatDefinitionId = NAME_None;


	/** 当前选中的鱼漂实例 ID；后续鱼漂需要 Use/UnUse 时也沿用这份实例身份。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FloatItemInstanceId;

	/** 当前选中抄网的数字物品编号，0 表示未选择；权威装备组件写入并复制，UI 与抢抄能力读取，不代表独立装备栏或实例身份。 */
	UPROPERTY(BlueprintReadOnly)
	int32  ScoopNetItemId = 0;

	/** 旧英文物品身份，仅供旧资产和旧档案单向迁移读取；新运行逻辑不读写，转换后清空。 */
	UPROPERTY()
	FName ScoopNetDefinitionId = NAME_None;


	/** 当前选中的抄网实例 ID；装配和抄取用它确认玩家选择的是正式库存中的同一件物品。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ScoopNetItemInstanceId;

	/** 当前选中的鱼竿外观 ID；外观选择不进入库存物品数量。 */
	UPROPERTY(BlueprintReadOnly)
	FName RodSkinDefinitionId = NAME_None;

	/** 当前鱼竿耐久；没有合法鱼竿时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	double RodDurability = 0.0;

	/** 当前鱼竿实例是否已损坏；耐久耗尽后必须更换可用鱼竿才可继续使用。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;
};
