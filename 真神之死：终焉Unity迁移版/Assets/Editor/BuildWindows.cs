using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

public static class BuildWindows
{
    public static void Build()
    {
        Debug.Log("BuildWindows.Build started.");
        PlayerSettings.companyName = "hhhh";
        PlayerSettings.productName = "真神之死：终焉";
        PlayerSettings.SplashScreen.show = false;
        PlayerSettings.SplashScreen.showUnityLogo = false;
        PlayerSettings.fullScreenMode = FullScreenMode.Windowed;
        PlayerSettings.defaultIsNativeResolution = false;
        PlayerSettings.defaultScreenWidth = 1500;
        PlayerSettings.defaultScreenHeight = 900;
        PlayerSettings.resizableWindow = true;
        Directory.CreateDirectory("Assets/Scenes");
        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        EditorSceneManager.SaveScene(scene, "Assets/Scenes/Main.unity");
        var buildPath = Path.GetFullPath("Build/WorldPveUnity_HearthUI.exe");
        Directory.CreateDirectory(Path.GetDirectoryName(buildPath));
        var report = BuildPipeline.BuildPlayer(new[] { "Assets/Scenes/Main.unity" }, buildPath, BuildTarget.StandaloneWindows64, BuildOptions.None);
        Debug.Log("BuildWindows.Build finished: " + report.summary.result + " -> " + buildPath);
        if (report.summary.result != UnityEditor.Build.Reporting.BuildResult.Succeeded)
        {
            EditorApplication.Exit(1);
        }
    }
}
