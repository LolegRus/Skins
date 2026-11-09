// Цвета вершин + простой свет по нормали — для пещеры-подложки (Tools → Skins → Hub Cave Scene) и для
// просмотра моделей Cinder в редакторе так, как их красит игра (альфа 0 — цвет команды, здесь фиолетовым)
Shader "Skins/Vertex Color"
{
    SubShader
    {
        Tags { "RenderType" = "Opaque" }
        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            struct appdata { float4 vertex : POSITION; float3 normal : NORMAL; float4 color : COLOR; };
            struct v2f { float4 pos : SV_POSITION; float4 color : COLOR; float3 normal : TEXCOORD0; };

            v2f vert (appdata v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.color = v.color;
                o.normal = UnityObjectToWorldNormal(v.normal);
                return o;
            }

            fixed4 frag (v2f i) : SV_Target
            {
                float3 team = float3(0.45, 0.1, 0.9);
                float3 c = lerp(team, i.color.rgb, i.color.a);
                float light = 0.55 + 0.45 * saturate(dot(normalize(i.normal), normalize(float3(0.3, 1, -0.4))));
                return fixed4(c * light, 1);
            }
            ENDCG
        }
    }
}
