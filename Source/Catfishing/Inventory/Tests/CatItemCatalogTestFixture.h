#pragma once

#include "Inventory/CatInventorySettings.h"
#include "UObject/StrongObjectPtr.h"

/** 测试期间替换正式查询入口的总表；只操作内存副本，退出作用域恢复原配置，不保存正式资产。 */
struct FCatItemCatalogTestFixture
{
	/** 测试操作的设置对象；构造时取得，析构时只恢复它的总表引用。 */
	UCatInventorySettings* Settings = GetMutableDefault<UCatInventorySettings>();
	/** 测试前的总表软引用；保留原路径，让后续用例仍读取正式资产。 */
	TSoftObjectPtr<UDataTable> Original = Settings->ItemCatalog;
	/** 当前测试独占并保活的表；测试增加和移除物品不影响其他资产。 */
	TStrongObjectPtr<UDataTable> Table;

	/** 默认复制正式表；空表模式用于完全自包含的夹具。 */
	explicit FCatItemCatalogTestFixture(bool bCopyFormal = true)
	{
		// 先建立独立表再切换设置；强引用覆盖整个测试作用域，避免软引用指向已回收对象。
		UDataTable* Source = bCopyFormal ? Original.LoadSynchronous() : nullptr;
		Table.Reset(Source ? DuplicateObject<UDataTable>(Source, GetTransientPackage()) : NewObject<UDataTable>());
		Table->RowStruct = FCatItemCatalogRow::StaticStruct();
		Settings->ItemCatalog = Table.Get();
	}

	/** 退出时恢复正式表，临时表随后由强引用释放。 */
	~FCatItemCatalogTestFixture()
	{
		Settings->ItemCatalog = Original;
	}

	/** 以定义自己的数字编号登记或替换一行；查询仍经过生产总表校验。 */
	void Add(UCatInventoryItemDefinition* Definition)
	{
		// 不为测试绕开身份一致性；行名、编号和定义均来自同一个测试定义。
		FCatItemCatalogRow Row;
		Row.ItemId = Definition->ItemId;
		Row.ItemDefinition = Definition;
		Table->AddRow(FName(*FString::FromInt(Row.ItemId)), Row);
	}

	/** 删除指定测试行以构造缺数据场景；不修改定义对象。 */
	void Remove(int32 ItemId)
	{
		Table->RemoveRow(FName(*FString::FromInt(ItemId)));
	}
};
