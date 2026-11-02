using MelonLoader;

[assembly: MelonInfo(typeof(Skins.Core), "Skins", "0.1.0", "LolegRus", null)]
[assembly: MelonGame("VRDojo", "Dust")]

namespace Skins
{
    public class Core : MelonMod
    {
        public override void OnInitializeMelon()
        {
            LoggerInstance.Msg("Initialized.");
        }
    }
}
