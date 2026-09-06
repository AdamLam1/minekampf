# Research: look & feel Minecraft / Hytale — wnioski dla zespołu (2026-09-06)

Autor: studio-head sesja QA/graphics. Cel: eliminacja „szklanego", sztucznego
wyglądu; zdefiniowanie kierunku artystycznego na wzór MC/Hytale.

## 1. Model oświetlenia Minecraft vanilla (potwierdzone źródłami)

- **Fixed face shading** (hardcoded multiplikatory jasności ścian):
  top **1.0**, north/south **0.8**, east/west **0.6**, bottom **0.5**.
  To jest szkielet czytelności voxeli — stosowane ZAWSZE, nawet bez smooth
  lighting. (Źródła: minecraft.wiki/w/Light, Builders QoL shader thread.)
  → Nasz chunk.frag po poprawce: 1.0 / 0.82 / 0.65 / 0.55 — świadomie lekko
  jaśniejsze boki, żeby nie spychać albedo do ciemności przy naszych jasnych
  teksturach. Trzymać.
- **Brak specularu na terenie** — teren jest czysto diffuse. Specular tylko
  woda + materiały autorskie (metale). → Wprowadzone (metal > 0.5 gate).
- **Smooth lighting / AO**: per-vertex AO z 3 sąsiadów (side1, side2, corner)
  — klasyka: 0fps.net „Ambient occlusion for Minecraft-like worlds". Nasz
  v_light.z to już implementuje; podnieśliśmy siłę (floor 0.45→0.30) i
  obniżyliśmy ambient (0.22→0.16), żeby narożniki były czytelne.
- **Lightmapa**: sky 0–15, block 0–14; MC stosuje nieliniową krzywą na
  brightness. U nas: gamma 1.45 na block light — zachować.

## 2. Hytale — kierunek artystyczny (dev blog + docs)

- Oficjalna definicja: **„a modern, stylized voxel game, with retro
  pixel-art textures"** (hytale.com/news/2025/12 An Introduction to Making
  Models for Hytale).
- **Ciepłe, nasycone kolory z ZABARWIONYMI CIENIAMI** — w cieniach syntetyczne
  tony (np. fiolet/zieleń) zamiast przyciemnionego odcienia bazowego
  (hytale-docs.com Textures Guide). To najbardziej wpływowy trik stylizacji,
  którego jeszcze NIE mamy.
- Stylized rendering = bardziej nasycone kolory + jaśniejsze światła niż
  realizm (80.lv stylization deep-dive).
- Modele: spójna „hand-painted" estetyka zamiast prostych kolorów — Blockbench
  pipeline, który mamy, jest zgodny z oficjalnym toolchainem Hytale
  (Blockbench Modeling Guide).

## 3. Co wprowadziliśmy (fala 1, ta sesja)

| Zmiana | Plik | Efekt |
|---|---|---|
| Usunięcie dielektrycznego GGX z terenu (specular tylko metale) | chunk.frag | koniec „szklanych" bloków |
| Fixed face shading a la MC | chunk.frag | czytelne ściany, głębia |
| Saturacja/contrast albedo (+22%/+7%) | chunk.frag | tekstury nie są blade |
| Normal map terenu ×0.55 | chunk.frag | mniej „brudnych" plam na ścianach |
| AO floor 0.45→0.30, ambient 0.22→0.16 | chunk.frag | czytelne narożniki i cienie |
| Woda: głębszy kolor, Fresnel cap 0.90→0.80, alpha −0.08 | chunk.frag | woda = woda, nie lustro-szkło |
| Niebo: zenith (0.36,0.56,0.94), fog (0.60,0.77,0.95) | renderer.cpp | głębszy dzień, kontrast sylwetek |
| Saturacja post 1.12→1.20 + łagodna S-krzywa | post.frag | spójny stylizowany grade |
| Fog w poście z uniformu (≠ hardcoded) | post.frag+renderer | koniec twardej linii woda/niebo |
| Usunięty stray override `bin/assets/textures/Stone.png` (magenta) | build artifacts | koniec missing-texture na stone |

## 4. Rekomendacje fali 2 (do rozdysponowania)

1. **Tinted shadows** (Hytale): cienie/skylight cienie podbić chłodnym
   fioletem zamiast czerni — chunk.frag, tam gdzie mnożymy przez shadow.
   Kandydat: `shadow_col = mix(vec3(1.0), vec3(0.62,0.60,0.78), shadow)`.
2. **Cząsteczki pogody**: deszcz renderowany jako czarne kwadraciki BlockDust
   bez blendingu — wydłużone krople + additive/alpha blend (particle_system).
3. **Modele graczy**: proceduralny humanoid fallback jest OK, ale player
   species nie ma pliku modelu (`no model for player — procedural fallback`
   w logu). Blokbench: `player.bbmodel` 6 kości (głowa/tułów/2×ręka/2×noga)
   + skin 64×64 — technical-artist.
4. **Zachód słońca jako event artystyczny**: obniżyć saturację nieba w zenith
   przy niskim słońcu, przenieść nasycenie do horyzontu (już częściowo jest).
5. Trzymać zasady: żadnego specularu na terenie, żadnego czystego czarnego
   w cieniu (min. ambient niebieski), saturacja albedo przed światłem.

## Źródła

- https://minecraft.wiki/w/Light
- https://0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/
- https://hytale.com/news/2025/12/an-introduction-to-making-models-for-hytale
- https://hytale-docs.com/docs/modding/art-assets/textures
- https://80.lv/articles/stylization-in-video-games-a-deep-dive-analysis
- https://britakee-studios.gitbook.io/hytale-modding-documentation/packs-content-creation/17-blockbench-modeling-guide
