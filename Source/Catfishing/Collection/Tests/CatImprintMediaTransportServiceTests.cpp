#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Collection/CatImprintMediaSettings.h"
#include "Collection/CatImprintMediaTransportService.h"

namespace CatImprintMediaTransportServiceTest
{
	/** 临时开启媒体传输配置的测试守卫；只改默认对象内存，不写配置文件。 */
	struct FMediaSettingsOverride
	{
		/** 被测试覆盖的默认设置对象。 */
		UCatImprintMediaSettings* Settings = GetMutableDefault<UCatImprintMediaSettings>();

		/** 原始运行 gate。 */
		bool bOldEnabled = false;

		/** 原始媒体大小上限。 */
		int64 OldMaxMediaBytes = 0;

		/** 原始块大小上限。 */
		int32 OldMaxChunkBytes = 0;

		/** 原始块数量上限。 */
		int32 OldMaxChunkCount = 0;

		/** 原始 MIME 白名单。 */
		TArray<FString> OldMimeTypes;

		// 保存流程：记录旧配置后开启一个小尺寸 PNG 媒体通道，测试只在内存默认对象上运行。
		FMediaSettingsOverride()
		{
			if (Settings)
			{
				bOldEnabled = Settings->bEnableImprintMediaTransport;
				OldMaxMediaBytes = Settings->MaxMediaBytes;
				OldMaxChunkBytes = Settings->MaxChunkBytes;
				OldMaxChunkCount = Settings->MaxChunkCount;
				OldMimeTypes = Settings->AllowedMimeTypes;
				Settings->bEnableImprintMediaTransport = true;
				Settings->MaxMediaBytes = 16;
				Settings->MaxChunkBytes = 4;
				Settings->MaxChunkCount = 8;
				Settings->AllowedMimeTypes = {TEXT("image/png")};
			}
		}

		// 恢复流程：还原所有默认对象字段，避免影响后续自动化用例。
		~FMediaSettingsOverride()
		{
			if (Settings)
			{
				Settings->bEnableImprintMediaTransport = bOldEnabled;
				Settings->MaxMediaBytes = OldMaxMediaBytes;
				Settings->MaxChunkBytes = OldMaxChunkBytes;
				Settings->MaxChunkCount = OldMaxChunkCount;
				Settings->AllowedMimeTypes = OldMimeTypes;
			}
		}
	};

	/** 临时关闭媒体传输默认配置的测试守卫；fail-closed 用例必须模拟空配置，而不是读取项目 Work7 默认值。 */
	struct FMediaSettingsDisabledOverride
	{
		/** 被测试覆盖的默认设置对象；服务入口通过 GetDefault 读取它。 */
		UCatImprintMediaSettings* Settings = GetMutableDefault<UCatImprintMediaSettings>();

		/** 原始运行 gate；析构时恢复。 */
		bool bOldEnabled = false;

		/** 原始媒体大小上限；析构时恢复。 */
		int64 OldMaxMediaBytes = 0;

		/** 原始块大小上限；析构时恢复。 */
		int32 OldMaxChunkBytes = 0;

		/** 原始块数量上限；析构时恢复。 */
		int32 OldMaxChunkCount = 0;

		/** 原始 MIME 白名单；析构时恢复。 */
		TArray<FString> OldMimeTypes;

		/** 构造流程：保存项目默认值后写入一套显式关闭的媒体配置，让默认 gate 用例只观察关闭语义。 */
		FMediaSettingsDisabledOverride()
		{
			if (Settings)
			{
				bOldEnabled = Settings->bEnableImprintMediaTransport;
				OldMaxMediaBytes = Settings->MaxMediaBytes;
				OldMaxChunkBytes = Settings->MaxChunkBytes;
				OldMaxChunkCount = Settings->MaxChunkCount;
				OldMimeTypes = Settings->AllowedMimeTypes;
				Settings->bEnableImprintMediaTransport = false;
				Settings->MaxMediaBytes = 0;
				Settings->MaxChunkBytes = 0;
				Settings->MaxChunkCount = 0;
				Settings->AllowedMimeTypes.Reset();
			}
		}

		/** 析构流程：恢复项目默认媒体配置，避免影响同进程后续媒体与项目默认值测试。 */
		~FMediaSettingsDisabledOverride()
		{
			if (Settings)
			{
				Settings->bEnableImprintMediaTransport = bOldEnabled;
				Settings->MaxMediaBytes = OldMaxMediaBytes;
				Settings->MaxChunkBytes = OldMaxChunkBytes;
				Settings->MaxChunkCount = OldMaxChunkCount;
				Settings->AllowedMimeTypes = OldMimeTypes;
			}
		}
	};
	// 计划构造流程：生成一份字段完整的 CapturePlan；它只代表共同事件，不包含任何图片字节。
	static FCatCapturePlan MakePlan()
	{
		FCatCapturePlan Plan;
		Plan.CapturePlanId = FGuid::NewGuid();
		Plan.CandidateId = FGuid::NewGuid();
		Plan.RunId = FGuid::NewGuid();
		Plan.EventType = TEXT("CaughtTogether");
		Plan.SubjectId = FGuid::NewGuid();
		Plan.RunAlbumId = FGuid::NewGuid();
		return Plan;
	}

	// 授权构造流程：给两个收件人同一 Membership/Permission 版本，用来验证独立 cursor 和版本重验。
	static TArray<FCatImprintMediaRecipientAuthorization> MakeRecipients()
	{
		FCatImprintMediaRecipientAuthorization First;
		First.RecipientStableNetId = TEXT("PlayerA");
		First.MembershipRevision = 7;
		First.PermissionRevision = 11;

		FCatImprintMediaRecipientAuthorization Second;
		Second.RecipientStableNetId = TEXT("PlayerB");
		Second.MembershipRevision = 7;
		Second.PermissionRevision = 11;
		return {First, Second};
	}

	// Manifest 构造流程：按测试媒体字节和块大小推导 Manifest，hash 来自真实媒体服务 helper。
	static FCatImprintMediaManifest MakeManifest(const FCatCapturePlan& Plan, const FGuid MediaId,
		const TArray<uint8>& Bytes, const int32 ChunkSize)
	{
		FCatImprintMediaManifest Manifest;
		Manifest.MediaId = MediaId;
		Manifest.CandidateId = Plan.CandidateId;
		Manifest.RunAlbumId = Plan.RunAlbumId;
		Manifest.MimeType = TEXT("image/png");
		Manifest.TotalSizeBytes = Bytes.Num();
		Manifest.ChunkSizeBytes = ChunkSize;
		Manifest.ChunkCount = (Bytes.Num() + ChunkSize - 1) / ChunkSize;
		Manifest.Sha256Hex = UCatImprintMediaTransportService::ComputePayloadHashHex(Bytes);
		return Manifest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatImprintMediaTransportSettingsGateTest,
	"Catfishing.Unit.Collection.ImprintMedia.SettingsGateFailsClosedByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatImprintMediaTransportManifestChunkAndCursorTest,
	"Catfishing.Unit.Collection.ImprintMedia.ManifestChunksHashCursorAndRecipientAuth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：默认配置关闭时，即使计划和收件人完整也不能开始媒体传输，避免占位链路冒充产品成像。
bool FCatImprintMediaTransportSettingsGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatImprintMediaTransportServiceTest::FMediaSettingsDisabledOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建媒体默认 gate 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建媒体默认 gate 测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatImprintMediaTransportService* Service = World->GetSubsystem<UCatImprintMediaTransportService>();
	TestNotNull(TEXT("可取得媒体传输服务"), Service);
	if (!Service)
	{
		return false;
	}

	const FCatImprintMediaResult Result = Service->BeginHostMediaTransfer(FGuid::NewGuid(),
		CatImprintMediaTransportServiceTest::MakePlan(), TEXT("Host"),
		CatImprintMediaTransportServiceTest::MakeRecipients());
	TestEqual(TEXT("默认配置下媒体传输 fail-closed"), Result.Error, ECatDomainCommandError::PolicyUndecided);
	TestFalse(TEXT("默认配置不创建 MediaId"), Result.MediaId.IsValid());
	return !HasAnyErrors();
}

// 测试流程：开启配置后验证 Host 单次 Manifest、分块 hash、收件人授权版本、cursor/ACK 和重复 ACK 语义。
bool FCatImprintMediaTransportManifestChunkAndCursorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatImprintMediaTransportServiceTest::FMediaSettingsOverride SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建媒体传输测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建媒体传输测试 World"), World);
	if (!World)
	{
		return false;
	}

	UCatImprintMediaTransportService* Service = World->GetSubsystem<UCatImprintMediaTransportService>();
	TestNotNull(TEXT("可取得媒体传输服务"), Service);
	if (!Service)
	{
		return false;
	}

	const TArray<uint8> GoldenBytes = {static_cast<uint8>('a'), static_cast<uint8>('b'), static_cast<uint8>('c')};
	TestEqual(TEXT("SHA-256 helper 输出标准 abc 摘要"),
		UCatImprintMediaTransportService::ComputePayloadHashHex(GoldenBytes),
		FString(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));

	const FCatCapturePlan Plan = CatImprintMediaTransportServiceTest::MakePlan();
	const TArray<uint8> MediaBytes = {1, 2, 3, 4, 5, 6};
	TArray<FCatImprintMediaRecipientAuthorization> Recipients = CatImprintMediaTransportServiceTest::MakeRecipients();
	const FGuid BeginRequestId = FGuid::NewGuid();
	const FCatImprintMediaResult Begin = Service->BeginHostMediaTransfer(BeginRequestId, Plan, TEXT("Host"), Recipients);
	TestEqual(TEXT("配置开启后 Begin 成功"), Begin.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("Begin 分配 MediaId"), Begin.MediaId.IsValid());
	const FCatImprintMediaResult BeginReplay = Service->BeginHostMediaTransfer(BeginRequestId, Plan,
		TEXT("Host"), Recipients);
	TestEqual(TEXT("Begin 同 RequestId 同载荷变成安全重放"), BeginReplay.Error, ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("Begin 重放不重复 committed"), BeginReplay.bCommitted);
	TestEqual(TEXT("Begin 重放沿用同一 MediaId"), BeginReplay.MediaId, Begin.MediaId);
	TArray<FCatImprintMediaRecipientAuthorization> DriftRecipients = Recipients;
	DriftRecipients[0].PermissionRevision = 12;
	TestEqual(TEXT("Begin 同 RequestId 换收件人授权被拒绝"),
		Service->BeginHostMediaTransfer(BeginRequestId, Plan, TEXT("Host"), DriftRecipients).Error,
		ECatDomainCommandError::InvalidPayload);

	FCatImprintMediaManifest Manifest = CatImprintMediaTransportServiceTest::MakeManifest(Plan, Begin.MediaId, MediaBytes, 4);
	FCatImprintMediaManifest BadManifest = Manifest;
	BadManifest.MimeType = TEXT("image/jpeg");
	TestEqual(TEXT("未白名单 MIME 被拒绝"),
		Service->CommitHostMediaManifest(FGuid::NewGuid(), TEXT("Host"), BadManifest).Error,
		ECatDomainCommandError::InvalidPayload);

	const FGuid ManifestRequestId = FGuid::NewGuid();
	const FCatImprintMediaResult ManifestResult = Service->CommitHostMediaManifest(ManifestRequestId, TEXT("Host"), Manifest);
	TestEqual(TEXT("合法 Manifest 可冻结"), ManifestResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("Manifest 首次写入 committed"), ManifestResult.bCommitted);
	const FCatImprintMediaResult ManifestReplay = Service->CommitHostMediaManifest(ManifestRequestId, TEXT("Host"), Manifest);
	TestEqual(TEXT("Manifest 同 RequestId 同载荷变成安全重放"), ManifestReplay.Error, ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("Manifest 重放不重复 committed"), ManifestReplay.bCommitted);
	FCatImprintMediaManifest DriftManifest = Manifest;
	DriftManifest.ChunkSizeBytes = 2;
	DriftManifest.ChunkCount = 3;
	TestEqual(TEXT("Manifest 同 RequestId 换媒体合同被拒绝"),
		Service->CommitHostMediaManifest(ManifestRequestId, TEXT("Host"), DriftManifest).Error,
		ECatDomainCommandError::InvalidPayload);

	const TArray<uint8> FirstChunk = {1, 2, 3, 4};
	const TArray<uint8> SecondChunk = {5, 6};
	const FGuid FirstChunkRequestId = FGuid::NewGuid();
	const FCatImprintMediaResult FirstChunkResult = Service->CommitHostMediaChunk(FirstChunkRequestId, TEXT("Host"),
		Begin.MediaId, 0, FirstChunk);
	TestEqual(TEXT("第一块提交成功"), FirstChunkResult.Error, ECatDomainCommandError::None);
	TestFalse(TEXT("第一块后还不能给收件人读取"), FirstChunkResult.bReadyForRecipients);
	const FCatImprintMediaResult FirstChunkReplay = Service->CommitHostMediaChunk(FirstChunkRequestId, TEXT("Host"),
		Begin.MediaId, 0, FirstChunk);
	TestEqual(TEXT("Chunk 同 RequestId 同字节变成安全重放"), FirstChunkReplay.Error, ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("Chunk 重放不重复 committed"), FirstChunkReplay.bCommitted);
	const TArray<uint8> DriftFirstChunk = {9, 2, 3, 4};
	TestEqual(TEXT("Chunk 同 RequestId 换字节被拒绝"),
		Service->CommitHostMediaChunk(FirstChunkRequestId, TEXT("Host"), Begin.MediaId, 0, DriftFirstChunk).Error,
		ECatDomainCommandError::InvalidPayload);

	const FCatImprintMediaResult SecondChunkResult = Service->CommitHostMediaChunk(FGuid::NewGuid(), TEXT("Host"),
		Begin.MediaId, 1, SecondChunk);
	TestEqual(TEXT("第二块提交成功"), SecondChunkResult.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("所有块与整体 hash 匹配后开放给收件人"), SecondChunkResult.bReadyForRecipients);

	FCatImprintMediaCursor Cursor;
	FCatImprintMediaResult CursorResult = Service->GetRecipientCursor(Begin.MediaId, TEXT("PlayerA"), 7, 10, Cursor);
	TestEqual(TEXT("权限版本陈旧时不能读取 cursor"), CursorResult.Error, ECatDomainCommandError::RevisionConflict);
	CursorResult = Service->GetRecipientCursor(Begin.MediaId, TEXT("PlayerA"), 7, 11, Cursor);
	TestEqual(TEXT("正确授权版本可读取 cursor"), CursorResult.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("初始 cursor 指向第 0 块"), Cursor.NextChunkIndex, 0);

	FCatImprintMediaChunk Chunk;
	TestEqual(TEXT("不能跳读第 1 块"),
		Service->ReadRecipientChunk(Begin.MediaId, TEXT("PlayerA"), 7, 11, 1, Chunk).Error,
		ECatDomainCommandError::RevisionConflict);
	FCatImprintMediaResult ReadResult = Service->ReadRecipientChunk(Begin.MediaId, TEXT("PlayerA"), 7, 11, 0, Chunk);
	TestEqual(TEXT("按 cursor 可读第一块"), ReadResult.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("读取到第一块大小"), Chunk.Bytes.Num(), FirstChunk.Num());

	TestEqual(TEXT("ACK 负数块序号被拒绝"),
		Service->AcknowledgeRecipientChunk(FGuid::NewGuid(), Begin.MediaId, TEXT("PlayerA"), 7, 11,
			-1, TEXT("")).Error,
		ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("ACK 错 hash 被拒绝"),
		Service->AcknowledgeRecipientChunk(FGuid::NewGuid(), Begin.MediaId, TEXT("PlayerA"), 7, 11,
			0, FString::ChrN(64, TEXT('0'))).Error,
		ECatDomainCommandError::InvalidPayload);
	const FGuid AckFirstRequestId = FGuid::NewGuid();
	FCatImprintMediaResult AckFirst = Service->AcknowledgeRecipientChunk(AckFirstRequestId, Begin.MediaId,
		TEXT("PlayerA"), 7, 11, 0, Chunk.Sha256Hex);
	TestEqual(TEXT("ACK 第一块成功"), AckFirst.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("ACK 后 cursor 前进到第 1 块"), AckFirst.NextChunkIndex, 1);
	const FCatImprintMediaResult AckFirstReplay = Service->AcknowledgeRecipientChunk(AckFirstRequestId, Begin.MediaId,
		TEXT("PlayerA"), 7, 11, 0, Chunk.Sha256Hex);
	TestEqual(TEXT("ACK 同 RequestId 同 hash 变成安全重放"), AckFirstReplay.Error, ECatDomainCommandError::AlreadyResolved);
	TestFalse(TEXT("ACK 重放不重复 committed"), AckFirstReplay.bCommitted);
	TestEqual(TEXT("ACK 重放保留服务端 cursor"), AckFirstReplay.NextChunkIndex, 1);
	TestEqual(TEXT("ACK 同 RequestId 换 hash 被拒绝"),
		Service->AcknowledgeRecipientChunk(AckFirstRequestId, Begin.MediaId, TEXT("PlayerA"), 7, 11,
			0, FString::ChrN(64, TEXT('0'))).Error,
		ECatDomainCommandError::InvalidPayload);

	ReadResult = Service->ReadRecipientChunk(Begin.MediaId, TEXT("PlayerA"), 7, 11, 1, Chunk);
	TestEqual(TEXT("按新 cursor 可读第二块"), ReadResult.Error, ECatDomainCommandError::None);
	FCatImprintMediaResult AckSecond = Service->AcknowledgeRecipientChunk(FGuid::NewGuid(), Begin.MediaId,
		TEXT("PlayerA"), 7, 11, 1, Chunk.Sha256Hex);
	TestEqual(TEXT("ACK 第二块成功"), AckSecond.Error, ECatDomainCommandError::None);
	TestTrue(TEXT("PlayerA 完成自己的媒体副本"), AckSecond.bRecipientComplete);

	TestEqual(TEXT("PlayerB cursor 仍独立停在第 0 块"),
		Service->GetRecipientCursor(Begin.MediaId, TEXT("PlayerB"), 7, 11, Cursor).NextChunkIndex, 0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
