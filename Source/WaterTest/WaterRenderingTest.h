#pragma once

#include <GameEngineTest/TestClass/TestClass.h>
#include <RendererCore/Pipeline/RenderPipelineResource.h>
#include <RendererCore/Pipeline/ViewRenderMode.h>
#include <Texture/Image/Image.h>
#include <WaterPlugin/Resources/WaterProfileResource.h>

class plWaterRenderingTestGameState : public plGameEngineTestGameState
{
  PL_ADD_DYNAMIC_REFLECTION(plWaterRenderingTestGameState, plGameEngineTestGameState);

public:
  void SetRenderPipeline(const plRenderPipelineResourceHandle& hPipeline);
  bool SetViewRenderMode(plViewRenderMode::Enum mode);
};

/// \brief What every water test application needs: this package's plugin on the search path, its Assets mounted.
class plWaterTestApplication : public plGameEngineTestApplication
{
public:
  explicit plWaterTestApplication(const char* szProjectDirName);

  /// Where the SDK's binaries, this package's plugin and its Assets folder are. The test executable sits in the
  /// package, away from the engine's plugins (the renderer, for one), and is not installed anywhere. So it tells
  /// the application what an installation would have set up: plugin search roots, and Assets mounted as the data
  /// directory "plasma.water".
  static void SetPackageDirectories(plStringView sSdkBinaryDirectory, plStringView sPluginDirectory, plStringView sAssetsDirectory);

protected:
  virtual void Init_AddPluginSearchRoots() override;
  virtual void Init_FileSystem_ConfigureDataDirs() override;
};

class plWaterRenderingTestApplication : public plWaterTestApplication
{
public:
  plWaterRenderingTestApplication();

  enum class SceneVariant
  {
    AboveWater,
    Underwater,
    FlatLitWater, ///< Flat surface (amplitude 0, no spectrum) with a sun, so interaction ripples are the ONLY moving detail.
    TransmittedCaustics,
    ReflectedCaustics,
    OceanReflectedCausticsSuppressed,
    LakeJunction,
    OceanDebugModes,
    FiniteDebugModes,
  };

  plResult SetupScene(SceneVariant variant);

  /// Switches the projected-caustic test fixture between otherwise identical zero-strength and enabled profiles.
  bool SetProjectedCausticsEnabled(bool bEnabled);
  bool EnableLakeJunction();
  bool HasValidLakeJunction();
  bool SetViewRenderMode(plViewRenderMode::Enum mode);

  /// Queues a water interaction like the editor interact tool / plWaterInteractorComponent would.
  /// fStrengthOverride < 0 keeps the tool defaults (splash 1.0 / continuous 0.3).
  void InjectWaterInteraction(const plVec3& vPosition, bool bSplash, float fStrengthOverride = -1.0f);

  /// Current fixed-step tick of the water simulation (60 Hz), for timing test phases in sim time.
  plUInt64 GetWaterSimulationTick();

  /// Creates an object with a plWaterInteractorComponent (component defaults: Splash type, strength 1).
  plGameObjectHandle CreateWaterInteractor(const plVec3& vPosition);
  void SetObjectPosition(const plGameObjectHandle& hObject, const plVec3& vPosition);
  /// Counts interaction events the world module recorded after ref_uiCursor (advances the cursor).
  void CountInteractionsSince(plUInt64& ref_uiCursor, plUInt32& out_uiTotal, plUInt32& out_uiSplashes);

protected:
  virtual plUniquePtr<plGameStateBase> CreateGameState() override;
  virtual void BeforeHighLevelSystemsShutdown() override;

private:
  plRenderPipelineResourceHandle m_hRenderPipeline;
  plGameObjectHandle m_hWaterObject;
  plGameObjectHandle m_hRiverObject;
  plGameObjectHandle m_hJunctionObject;
  plWaterProfileResourceHandle m_hCausticsDisabledProfile;
  plWaterProfileResourceHandle m_hCausticsEnabledProfile;
};

class plGameEngineTestWaterRendering : public plGameEngineTest
{
  using SUPER = plGameEngineTest;

public:
  virtual const char* GetTestName() const override;
  virtual plGameEngineTestApplication* CreateApplication() override;

protected:
  enum SubTests
  {
    NoLightAboveWater,
    NoLightUnderwater,
    InteractionDisplacement,
    TransmittedCaustics,
    ReflectedCaustics,
    OceanReflectedCausticsSuppressed,
    LakeJunctionContinuity,
    OceanDebugModes,
    FiniteDebugModes,
  };

  virtual void SetupSubTests() override;
  virtual plResult InitializeSubTest(plInt32 iIdentifier) override;
  virtual plTestAppRun RunSubTest(plInt32 iIdentifier, plUInt32 uiInvocationCount) override;

private:
  bool AssertNonBlackWaterImage() const;
  plTestAppRun RunInteractionDisplacementTest();
  plTestAppRun RunProjectedCausticsTest(bool bReflected, bool bExpectVisible = true);
  plTestAppRun RunLakeJunctionContinuityTest();
  plTestAppRun RunSurfaceDebugModesTest(bool bFiniteWater);

  plWaterRenderingTestApplication* m_pOwnApplication = nullptr;
  plInt32 m_iFrame = -1;
  plImage m_BaselineImage;
  plImage m_DecayedImage;
  float m_fFirstWakeChangedFraction = 0.0f;
  float m_fStreamChangedFraction = 0.0f;
  plInt32 m_iLifecyclePhase = 0;
  plUInt64 m_uiPhaseStartTick = 0;
  plUInt64 m_uiLastStreamInjectTick = 0;
  plUInt64 m_uiInteractionCursor = 0;
  plGameObjectHandle m_hProberObject;
  plVec3 m_vProberStart = plVec3::MakeZero();
};
