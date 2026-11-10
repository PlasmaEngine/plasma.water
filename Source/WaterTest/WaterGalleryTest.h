#pragma once

#include <WaterTest/WaterRenderingTest.h>

/// \brief Scenes for looking at water, and for the checks that need a whole frame.
///
/// Every scene is built in code and rendered through the project's main render pipeline, which no test touches: the
/// water passes are in it only because the plugin registered them with the engine. Each shot is saved as a PNG under
/// the test output folder (Gallery/), so a change to waves, shading or stitching can be looked at, not only measured.
class plWaterGalleryApplication : public plWaterTestApplication
{
public:
  plWaterGalleryApplication();

  enum class Scene
  {
    OpenOcean,   ///< Deep water to the horizon with a few rocks, at three wind strengths.
    Beach,       ///< A sand slope running into the sea: shore fade, shore foam and depth tint.
    ShadedCove,  ///< A cliff between a low sun and the water: what water does in shadow.
    RiverNetwork, ///< A lake, a river that leaves it and reaches the sea: junctions at both ends of one river.
    RiverBend,   ///< A river through two bends: flow direction, detail and foam advection.
  };

  struct Shot
  {
    const char* m_szName;
    plVec3 m_vPosition;
    plVec3 m_vTarget;
  };

  plResult SetupScene(Scene scene, plUInt32 uiVariant = 0);
  plArrayPtr<const Shot> GetShots(Scene scene) const;
  void SetShot(const Shot& shot);

  /// Saves the last screenshot as <output>/Gallery/<name>.png.
  plResult SaveShot(plStringView sName);

protected:
  virtual plUniquePtr<plGameStateBase> CreateGameState() override;
  virtual void BeforeHighLevelSystemsShutdown() override;

private:
  plGameObject* AddBox(const char* szName, const plVec3& vCenter, const plVec3& vSize, const plColor& color, const plQuat& qRotation = plQuat::MakeIdentity());
  plGameObject* AddWaterBody(const char* szName, plWaterBodyType::Enum type, const plWaterProfileResourceHandle& hProfile, plArrayPtr<const plVec3> spline, bool bClosed);
  void AddJunction(plGameObject* pRiver, plGameObject* pOther, const plVec3& vPosition, bool bRiverEnd);
  void AddSunAndSky(const plVec3& vToSun, float fLux);

  plMeshResourceHandle m_hBoxMesh;
  plUInt32 m_uiProfileCounter = 0;
};

class plGameEngineTestWaterGallery : public plGameEngineTest
{
  using SUPER = plGameEngineTest;

public:
  virtual const char* GetTestName() const override;
  virtual plGameEngineTestApplication* CreateApplication() override;

protected:
  enum SubTests
  {
    MainPipelineDrawsWater,
    OpenOceanCalm,
    OpenOceanBreeze,
    OpenOceanStorm,
    Beach,
    ShadedCove,
    RiverNetwork,
    RiverBend,
  };

  virtual void SetupSubTests() override;
  virtual plResult InitializeSubTest(plInt32 iIdentifier) override;
  virtual plTestAppRun RunSubTest(plInt32 iIdentifier, plUInt32 uiInvocationCount) override;

private:
  plTestAppRun RunShots(plStringView sPrefix);
  plTestAppRun RunMainPipelineDrawsWater();

  plWaterGalleryApplication* m_pOwnApplication = nullptr;
  plWaterGalleryApplication::Scene m_Scene = plWaterGalleryApplication::Scene::OpenOcean;
  plInt32 m_iFrame = -1;
  plUInt32 m_uiShot = 0;
  plInt32 m_iShotFrame = 0;
  plInt32 m_iPhase = 0;
  plImage m_WithWater;
  plImage m_WithoutInjections;
};
