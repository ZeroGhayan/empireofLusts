# Modo de voo: estado, plano, custo e desempenho

Revisão de 26/11/2026, feita sobre a cópia do `~/empireofLust` do keep-os.
Essa cópia tinha cerca de 1.844 linhas não commitadas em relação ao GitHub
(`84446df`) e não compilava.

## 1. Estado atual (antes desta revisão)

**Onde está:**
- **GitHub** `ZeroGhayan/empireofLusts`, `main` em `84446df` (24/09).
- **keep-os** `~/empireofLust`, com o mesmo HEAD e mais 12 arquivos modificados
  e `engine/src/sno_embed.c` novo. É o estado mais recente.
- `Ref/` (1,4 GB) guarda as referências, a especificação de 23/09 e os assets
  de terceiros. Os tiles DP1, SS4 e SNO, `pilot_src` e `tools/*.png` não
  estão versionados e não devem ser publicados.

**Motor:** é o EXO ("Ghayan Engine"), em C99 com libctru, citro2d e citro3d.
- O voo é pseudo-3D. A CPU projeta cada canto de cada tile (`flight_proj.c`),
  e o resultado é desenhado como triângulos 2D. O piso texturizado usa um
  shader citro3d próprio (`texfloor.v.pica`) num buffer de vértices em
  espaço de tela. Tiles sem textura, árvores, HUD e piloto usam o citro2d.
- Não há depth buffer. A ordem de desenho decide o que fica na frente.
- **Mapa:** `ExoTilemap` de até 128×128 células de 32 u, com altura por
  célula (`height[]`). Não há chunks nem streaming.
- **Câmera:** fica atrás da piloto, com yaw, pitch (±0,28 rad) e o roll da
  tela aplicado como rotação 2D. O 3D estéreo projeta o mundo de novo para o
  olho direito.

**Voo existente (`flight.c`):**
- Modos LOW e HIGH com histerese em 100/52 u/s, como pede a especificação R14.
- No HIGH em solo, B decola. No ar, o stick mexe no pitch e no roll do corpo,
  L/R funcionam como leme, Y acelera e A freia.
- Sem acelerar, a velocidade cai até 0. Abaixo de 52 a piloto sai do HIGH e
  só cai. Ou seja, é um "carro voador": para no ar e não tem planeio nem
  estol.
- **Pilotos:** Shirammy (vmax 380) e Rexxi (vmax 200, aceleração menor).

**Mapas de teste:**
- **TEST:** física e câmera, com rampas, rampa exponencial, gelo, água,
  piso duplo e mola. Fora da faixa central é vazio, então voar ali cai e
  reinicia.
- **SS4, DP1 e SNO:** pistas com tiles de terceiros.
- **DEMO:** plano aberto de 128×128 com árvores, que não estava no menu.

**Glitches encontrados (causas confirmadas no código):**

| Sintoma | Causa |
|---|---|
| Tela de baixo some ou pisca, às vezes o piloto também | O minimapa desenhava 96×96 = 9.216 retângulos por quadro, mais que os 8.192 objetos do `C2D_Init` |
| Morros e rampas distantes cobrindo o chão perto, piorando com o RENDER R | Anéis de tiles desenhados de perto para longe, sem depth buffer |
| Faixas e tiles "fantasma", olho esquerdo com a geometria do direito | `exo_texfloor_begin()` zerava o buffer de vértices a cada passe e olho, mas a GPU só lê tudo no `C3D_FrameEnd` |
| Tiles com textura errada (SNO: nuvens × tiles × tiles2) | O lote inteiro era desenhado com a última textura vista |
| Triângulo esticado ao inclinar a câmera | Vértice atrás do plano próximo, depois do pitch, era "grudado" em NEAR |
| Lentidão geral no New 3DS | Faltava `osSetSpeedupEnable(true)`: o .3dsx rodava a 268 MHz |

**Comparação com o Battle in Megaville:**
- É o mesmo esqueleto EXO (`engine/include/exo`, `platform_3ds.c`,
  `C3D_FRAME_SYNCDRAW`). O Megaville é 2D puro em citro2d, com
  `C2D_Init(16384)` e tiles 16×16 do Flash.
- O EoL é mais pesado por quadro: projeção na CPU, dois olhos e um shader
  próprio.
- O Megaville também não chama `osSetSpeedupEnable`. Vale testar lá
  primeiro, porque é uma linha só.
- Hipótese para as faixas verticais da tela de baixo no New 3DS real: o
  limite de objetos do C2D estourando na tela de baixo, como aconteceu aqui
  com o minimapa. O perfilador (`exo/prof.h`) foi feito para ser copiado
  igual para lá. Basta contar os objetos desenhados por tela.

> A especificação de 23/09 (seção 10) fala em "F-Zero / Special Stages de
> Sonic CD" para a velocidade alta. O design combinado depois a substitui:
> voo livre 3D tipo ORAS, sem parar no ar, e os mapas aéreos precisam disso.
> Vale atualizar a especificação.

## 2. Design combinado

- Voo 3D livre, como o soaring do Eon Flute em ORAS, mas sem nunca parar no
  ar. Dá para virar e voltar.
- Várias cidades e a dungeon final (a ilha flutuante) só são acessíveis pelo
  ar. O controle estilo Kid Icarus não serve.
- As wyverns voam como aviões: planar é o estado padrão, A bate as asas para
  ganhar impulso e subir, e há assistência anti-estol com velocidade mínima.
- **Shirammy** (história 1): velocidade final alta, asas longas, ótimo
  planeio e mergulho, curvas lentas. Fura a camada de gelo da montanha em
  alta velocidade.
- **Rexxi "Elizabete"** (história 2): aceleração alta, batidas explosivas
  (A), curvas fechadas. Passa a barreira repulsiva da ilha flutuante.
- Tela de baixo com mapa e marcador de destino, sem piloto automático, e
  zonas de pouso.

## 3. O que foi aplicado nesta revisão

1. **Compila de novo** (os dois erros do `platform_3ds.c`).
2. **Física de avião** (`engine/src/aero.c`), já ligada no tick quando a
   piloto está no ar em HIGH. Pode ser desligada no menu de pausa, em
   VOO: AVIÃO/ARCADE.
   - Planeio por padrão, A bate asas (segurar repete), B é freio
     aerodinâmico e L/R são leme.
   - Curva coordenada, com mergulho trocando altura por velocidade.
   - Estol baixa o nariz sozinho, e a velocidade tem piso de 0,85 × v_min.
   - Ganchos `exo_aero_can_pierce_ice()` (Shirammy) e
     `exo_aero_can_pass_barrier()` (Rexxi) para os mapas futuros.
3. **Correções de render:**
   - Ordem de pintura de trás para frente.
   - Buffer de vértices por quadro, lote fechado na troca de textura e flush
     de cache.
   - Descarte de vértice atrás da câmera depois do pitch.
4. **Neblina, distância de desenho e LOD:**
   - Neblina por vértice no shader do piso, e na CPU para tiles de cor sólida
     e árvores.
   - Blocos 4×4 de cor e altura médias até 3 × R.
   - "Saia" de neblina até o horizonte.
   - Liga e desliga em NEBLINA + LOD no menu de pausa.
5. **Minimapa pré-renderizado:** uma textura 128×128 montada uma vez por
   curso, com destino em cruz roxa e piloto com linha de rumo.
6. **Perfilador de quadro** com HUD na tela de baixo e CSV no SD (seção 6).
7. **Velocidade do New 3DS:** `osSetSpeedupEnable(true)`, que deixa a CPU a
   804 MHz com cache L2.
8. **Menu:** Y abre o DEMO (VOO LIVRE), o único mapa atual onde dá para voar
   sem cair no vazio. O destino do DEMO foi para um canto, para treinar ir e
   voltar.

Teste no Azahar (Xvfb), comparando antes e depois:
- A tela de baixo voltou a aparecer inteira.
- O voo no DEMO sobe, curva, plana e mergulha sem parar no ar.
- O chão vai até o horizonte com o LOD.
- O Azahar não mede desempenho real, então os números vêm do hardware.

## 4. O que falta em relação ao design

| # | Item | Esforço | Risco |
|---|---|---|---|
| 1 | Afinar os perfis Shirammy e Rexxi no hardware (números em `aero.c`) | 1 a 2 sessões com você jogando | baixo |
| 2 | Inversão do eixo Y como opção (hoje stick para cima = nariz para baixo, como avião) | 0,5 sessão | baixo |
| 3 | Mapa aéreo de verdade, maior que 128×128: chunks de voo 8×8 de guerra (especificação 7.x), streaming por anel e heightmap 9×9 por chunk com bordas compartilhadas | 4 a 6 sessões | médio (RAM e leitura de SD a 380 u/s) |
| 4 | Minimapa do mapa aéreo como imagem pré-renderizada por chunk (PNG para t3x), rolando com a piloto | 1 sessão | baixo |
| 5 | Zonas de pouso: células marcadas, pouso só abaixo de uma velocidade e ângulo, senão "toque e arremete" | 1 a 2 sessões | baixo |
| 6 | Camada de gelo da montanha (Shirammy fura acima de `pierce_speed`, senão ricocheteia) | 1 sessão | baixo |
| 7 | Barreira da ilha flutuante (Rexxi passa logo depois de uma batida, senão é empurrada) | 1 sessão | baixo |
| 8 | Ilha flutuante e cidades altas: blocos de terreno acima do chão (o piso duplo do TEST já prova a ideia) | 2 a 3 sessões | médio (ordem de pintura de objetos altos) |
| 9 | Objetos distantes como billboards (cidades, torres) e LOD de árvores | 1 a 2 sessões | baixo |
| 10 | Projeção única para os dois olhos (o olho direito só desloca x por profundidade) | 1 sessão | baixo, e corta cerca de 40% do custo do top_r |
| 11 | Depth buffer no piso (z por vértice): dispensa a ordenação e permite objetos 3D cruzando o piso | 2 sessões | médio (misturar com a profundidade do citro2d) |
| 12 | Câmera de voo: afastar e subir com a velocidade e ver por trás na curva (referencial SUPERFÍCIE/PILOTO já existe) | 1 sessão | baixo |

**Total para "voo completo jogável num mapa aéreo":** cerca de 15 a 22 sessões.
Uma sessão é um bloco de trabalho meu de 1 a 2 horas, mais o seu teste no
hardware. O caminho crítico é o item 3, o mapa aéreo com chunks.

## 5. Orçamento do 3DS e plano de otimização

- **Quadro:** 16,6 ms a 60 fps ou 33,3 ms a 30 fps.
- **Alvo de FPS (decidido em 26/11):** 30 fps estáveis com o 3D ligado e
  60 fps com o 3D desligado.
- **Disponibilidade do 3D no voo:** só nos modelos New 3DS. No Old 3DS o
  efeito fica forçado desligado (`gfxSet3D(false)` e o slider é lido como
  0): o portátil antigo não tem CPU para o segundo olho no voo, e o alvo
  nele é 30 fps com o 3D off (e raio de render menor, se precisar).

| Recurso | Old 3DS | New 3DS | Uso atual estimado (R=15) |
|---|---|---|---|
| CPU ARM11 | 268 MHz | 804 MHz + L2 (só com `osSetSpeedupEnable`) | projeção de cerca de 273 tiles × 4 cantos por olho e LOD com até cerca de 300 blocos |
| Vértices de GPU por quadro (prático) | cerca de 20 a 30 mil com shader simples | igual (mesma GPU, 268 MHz) | piso até 273 × 6 × 2 olhos ≈ 3,3 mil, mais citro2d |
| Desenhos (lotes) | dezenas é tranquilo | igual | 1 a 3 lotes de piso por olho e lotes do citro2d |
| Objetos citro2d | 8.192 (C2D_Init) | igual | antes eram mais de 9.216 só no minimapa; agora sobram centenas |
| Estereoscopia | desligada no voo | cerca de 2× o custo do top, só se o slider > 0 | top_r refaz toda a projeção; item 10 da tabela |

**Ordem do plano:**
1. Medir com o CSV no New 3DS: R = 8, 15 e 20; 3D desligado (meta 16,6 ms)
   e no máximo (meta 33,3 ms); Shirammy e Rexxi; neblina ligada e
   desligada. No Old 3DS o 3D não entra, então só a série sem 3D.
2. Se o tempo de CPU passar da meta (16,6 ms sem 3D, 33,3 ms com 3D):
   fazer o item 10 (projeção única) e baixar o `draw_cap`.
3. Se o wait for baixo e o gpu_draw alto: reduzir o fill. Fazer o LOD em uma
   única malha (triangle strip) e cortar a saia de neblina acima do horizonte.
4. No mapa aéreo, usar streaming por chunk de voo com no máximo 3×3 chunks
   residentes e carregar o anel seguinte em uma thread (`threadCreate`, no
   núcleo 1 do New 3DS).

## 6. Como testar no 3DS real

1. Copie `platforms/3ds/eol.3dsx` para o SD em `sdmc:/3ds/eol.3dsx` (por
   FTPD ou Transfer3DS). Abra pelo Homebrew Launcher.
2. No menu, A abre o TEST e Y abre o VOO LIVRE (DEMO, plano aberto).
3. Para decolar: segure o stick para frente e Y (carga) até aparecer HIGH,
   então aperte B. No ar:
   - A bate as asas; segurar repete.
   - Stick para trás sobe e para frente mergulha.
   - Stick para os lados inclina e curva, e L/R são o leme.
   - B segurado é o freio.
4. Para o perfil, use ZL (New 3DS) ou pausa > PERFIL. Ele alterna entre OFF,
   HUD e HUD + CSV.
   - **CSV:** grava em `sdmc:/3ds/eol/prof_000.csv`, com o número seguinte
     em cada sessão. Desligar o perfil (ou sair do jogo) fecha o arquivo.
   - **Colunas:** `frame, t_ms, frame_ms`, os tempos por fase
     (`input, tick, wait, top_l, top_r, bottom, submit`), depois
     `gpu_proc_ms, gpu_draw_ms`, os contadores
     (`tiles, culled, floor_verts, floor_batches, render_r, slider_pct`),
     `flush_ms` e `tag`.
   - **Tag:** 5 letras: curso D/S/4/N/T, piloto S/R, modo A (avião), H ou L,
     neblina F/f e New/Old N/o.
   - **CPU real** = `frame_ms - wait_ms`. Se `wait_ms` for quase 0 e
     `frame_ms` passar de 16,6, o jogo está abaixo de 60 fps.
5. **Roteiro sugerido de cerca de 2 minutos (New 3DS):** voe 30 s com o
   3D desligado (meta 60 fps), 30 s com o 3D no máximo (meta 30 fps),
   mude o RENDER R (toque na barra de baixo) para 8 e depois 20, e
   repita com a neblina desligada. Depois me mande os arquivos
   `prof_*.csv`. No Old 3DS o 3D não liga; o roteiro fica só com o
   slider fechado.
