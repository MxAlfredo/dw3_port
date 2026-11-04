#!/bin/sh
# Owner review shortcuts: each scenario boots straight into one scene, with
# sound, and stays playable from the keyboard (the harness only overrides the
# pad while a scripted key is held). Prints what to look and listen for.
#
#   pc/review.sh <scenario> [disc.bin]
#
# Scenarios: batalla, resultados, cambio, libre, inicio, digilab, pelicula, video2, menu, cargar, laboratorio, tecnica, tecnicacampo, nombre, gimnasio, tienda, tiendacartas, mazo, album, cartas, controles (alias: opciones).
# Every scenario ends by itself when its scene is done, read from the game's
# mode (harness tokens `until:<hex>`, implied by `mode:` and the battle tokens;
# the log prints "end: left mode 0x.. for 0x.. -> end of run"). Only `libre`
# stays open on purpose (token `stay`). Closing the window always ends a run.
set -e
cd "$(dirname "$0")/.."
disc="${2:-build/t9-e2e/DW3_EU_ES419_TEXT_TEST.bin}"
shot=pc/build/review.png

case "$1" in
batalla)
    keys="battle"
    echo "Batalla directa (Kotemon vs enemigo de nivel 12)."
    echo "Revisar: cámara y luz, nombres/HP/menú, cursor al moverte,"
    echo "música y efectos de los ataques (capas, que nada se corte)."
    echo "Termina solo al salir de la batalla."
    ;;
resultados)
    # The battle tokens' implied end covers the results scene (its own mode,
    # 0x1400): the run ends when the results are left.
    keys="battleauto"
    echo "Batalla ganada sola por el piloto automático (menú, Tech, mensajes)"
    echo "hasta la pantalla de resultados (EXP, dinero, objeto) y la vuelta al campo."
    echo "Termina solo al salir de la pantalla de resultados."
    echo "Revisar: textos de la pantalla de resultados, números de EXP y BIT,"
    echo "paneles de los tres compañeros, fundido final. El log trae 'auto N: ...'."
    ;;
cambio)
    keys="battle@150:down,10,down,10,down,10,cross"
    echo "Batalla con el menú de cambio (Tag) ya abierto: elegí el digimon."
    echo "Revisar: que el sonido del cambio termine solo, sin quedar en loop."
    echo "Termina solo al salir de la batalla."
    ;;
libre)
    # Progress 4 is the first one where START opens the field menu
    # (src/field/field_mode/field.c openFieldMenu: GAME.progress < 4 blocks it);
    # the warp's default progress is 0, so the menu stayed shut.
    keys="stay,warp:330,4"
    echo "Juego libre: Parque Central (escenario 330, progreso 4), abierto para caminar."
    echo "Start abre el menú del campo (Items, Sort, Map, Techniques, Status)."
    echo "Revisar: fluidez al caminar (60 FPS; Select o F1 > Display > Show FPS), escenario y música."
    echo "No termina solo, a propósito: cerrá la ventana cuando quieras."
    ;;
inicio)
    # The game as the player starts it: title, NEW GAME and the story from its
    # first scene, with no harness shortcut. `stay` keeps the run open.
    keys="stay"
    # one log line per battle turn and per paralysis roll (pc/shim/battle_trace.h)
    export DW3_BATTLE_EVENT_LOG=1
    echo "Juego desde el principio: título, NEW GAME e historia normal."
    echo "Revisar: lo que pida la tarea (por ejemplo Auto text en los primeros diálogos)."
    echo "No termina solo, a propósito: cerrá la ventana cuando quieras."
    ;;
digilab)
    keys="warp:200,3,flag:400D,fieldauto,until:D00"
    echo "Calle de Asuka tras la primera batalla (progreso 3): el señor de la salida"
    echo "habla de nuevo (el piloto de diálogos aprieta cruz solo) y el relato te lleva"
    echo "al Digilab. Revisar: que la transición termine en el Digilab y que puedas"
    echo "caminar ahí (antes quedabas congelado). El piloto se apaga al llegar al"
    echo "Digilab ('fieldauto N: off' en el log): desde ahí jugás vos."
    echo "Termina solo cuando abrís el laboratorio del doctor (0xD00) y lo cerrás."
    ;;
menu)
    keys="mode:1000"
    echo "Menú del campo (pantalla de objetos de STSTATUS, con el grupo inicial)."
    echo "Revisar: textos y paneles, cursor, cruz para entrar, triángulo para"
    echo "volver; \"mode:1000,4\" abre directo la pantalla de estado (0-5)."
    echo "Termina solo al cerrar el menú (triángulo hasta salir)."
    ;;
cargar)
    keys="mode:C00,0"
    echo "Pantalla de cargar (STGMCARD). Las partidas son archivos locales .dw3save:"
    echo "\$DW3_SAVES_DIR, o saves/default de la carpeta del usuario (~/.local/share/dw3port/dw3/; \$DW3_USER_DIR la cambia)."
    echo "Si no hay partidas y hay un .mcd en import/ (o \$DW3_MEMCARD) se importa una vez."
    echo "Con el slot 1: sin partidas = crear el archivo (solo al guardar); con partida = la lista."
    echo "Para guardar: pc/build/boot con \"mode:C00,save\"."
    echo "Termina solo al cerrar la pantalla (al volver al campo o al título)."
    ;;
laboratorio)
    keys="mode:D00"
    echo "Laboratorio Digimon (STGDGLAB) con el grupo inicial, como al hablar con el"
    echo "doctor en el campo (vuelve al campo 0x206 al salir). Revisar: las tres"
    echo "opciones (cambiar, digievolucionar, tabla), cruz para entrar, triángulo"
    echo "para volver. Sin entradas aprendidas, 'Digivolve' dice que no se puede"
    echo "cargar una técnica (el estado de partida nueva)."
    echo "Termina solo al salir del laboratorio (triángulo en el menú)."
    ;;
tecnica)
    keys="partner:0,3,2,battle"
    echo "Batalla directa donde el primer compañero está en la forma 3 (nivel 2), la que aprende"
    echo "la técnica del mod sample-tech (Golpe chispa / Spark Strike). Pide el mod ACTIVO en"
    echo "Opciones > Mods (el piloto no toca tu settings.ini). Revisar: Tech en el menú de"
    echo "comandos muestra la técnica nueva con su coste de 3 PM, y se puede usar."
    echo "Token: partner:<miembro>,<forma>[,<nivel>] (forma = id de Digimon; ver docs/modding)."
    echo "Termina solo al salir de la batalla."
    ;;
tecnicacampo)
    keys="partner:0,3,2,40,mode:1000,3,stay"
    echo "Pantalla Techniques del menú del campo (mode:1000,3) con el primer compañero en la forma 3"
    echo "(nivel 2, PV al 40 %), que aprende la técnica curativa del mod sample-tech (Chispa sanadora /"
    echo "Mend Spark, basada en una curación original). Pide el mod ACTIVO en Opciones > Mods."
    echo "Revisar: la técnica sale en la lista, se elige, se elige objetivo y los PV suben (PM baja 2)."
    echo "Token: partner:<miembro>,<forma>[,<nivel>[,<PV %>]] (ver README)."
    echo "Se queda abierto (stay): cierra la ventana al terminar."
    ;;
nombre)
    keys="mode:B00"
    echo "Poner nombre al compañero (STDGNAME) con el grupo inicial. Revisar: elegir"
    echo "digimon con cruz, teclado, cruz escribe, start salta a OK, cruz confirma;"
    echo "el nombre nuevo sale en la lista; triángulo vuelve al campo."
    echo "Termina solo al salir de la pantalla."
    ;;
gimnasio)
    keys="mode:A00,0"
    echo "Gimnasio (STGTRAIN, gimnasio 0 = Asuka centro; \"mode:A00,<0-7>\" cambia)"
    echo "con 30 TP por compañero. Revisar: cruz elige digimon, entrenamiento y TP,"
    echo "\"Is this ok?\" y las tres tandas (cruz en cada intento), el mensaje de"
    echo "resultados (+fuerza), triángulo para salir al campo."
    echo "Termina solo al salir del gimnasio."
    ;;
tienda)
    keys="mode:F00,0"
    echo "Tienda de objetos (STITSHOP, tienda 0 = Asuka Armory) con 5000 BIT y un"
    echo "objeto de cada uno de los primeros 63 (para vender). Revisar: BUY/SELL,"
    echo "listas por categoría, precios, \"How many?\", confirmación Buy/Sell y que el"
    echo "dinero cambie (comprar resta, vender suma); triángulo para volver."
    echo "Termina solo al salir de la tienda (triángulo en el menú)."
    ;;
tiendacartas)
    keys="mode:1300,49"
    echo "Tienda de cartas (STCRDSHP, tienda 49 = AS Yellow Cruiser) con 50000 BIT."
    echo "Revisar: Buy One Card / Open Booster / Edit Folder; en la compra, la fila"
    echo "de cartas, precio (10000 BIT), cantidad en mazo y que el dinero baje."
    echo "Open Booster dice 'No boosters' (sin sobres en el estado de partida nueva)."
    echo "Termina solo al salir de la tienda (triángulo en el menú). Edit Folder abre el editor"
    echo "de mazo (otro modo, 0x400) y por eso termina la corrida: el editor se prueba con mazo."
    ;;
mazo)
    keys="mode:400,settle,battle@0:cross,cross,cross,cross,cross,triangle,triangle,triangle"
    echo "Editor de mazo (STCRDDEK) con el mazo inicial y 48 cartas más en la bolsa."
    echo "El piloto elige Folder 1, Edit Folder, reemplaza la primera carta del mazo"
    echo "por la primera de la lista y vuelve a salir. Revisar: la cuadrícula de 40"
    echo "cartas, el cambio de la primera carta (los cubos de color cambian) y la lista."
    echo "Termina solo al salir del editor (0x400)."
    ;;
album)
    keys="mode:1200,settle,battle@0:right,right,r1,triangle"
    echo "Álbum de cartas (STCRDABM) con 48 cartas vistas. El piloto mueve el cursor,"
    echo "pasa a la página 2 con R1 y sale. Revisar: página 1/27, nombre y texto de la"
    echo "carta bajo el cursor, el giro de página y que no falte ninguna imagen."
    echo "Termina solo al salir del álbum (0x1200)."
    ;;
cartas)
    keys="cardauto"
    echo "Combate de cartas (CARDGAME) contra el oponente 1 de Asuka City (modo 0x700, arg 3), con el"
    echo "mazo inicial. El piloto lee el estado del juego y aprieta lo que espera:"
    echo "elige carpeta, el sorteo de quién empieza, baja las cartas que puede pagar,"
    echo "responde Sí/No y los mensajes, hasta el fin del duelo (el log trae 'card N:' y"
    echo "'auto N:'). Revisar: mano de seis cartas con nombre, AP/HP de cada lado,"
    echo "cubos de puntos, el giro de las cartas del rival, las ventanas de ronda"
    echo "y el mensaje final. Dura unos 70 s. Termina solo al salir del combate (0x700)."
    ;;
controles|opciones)
    keys="stay,warp:330"
    echo "Menú de opciones (F1 o Select) sobre el juego libre (Parque Central)."
    echo "Pestañas Controls/Video/Display/Language/Audio con Q/E, RePág/AvPág o L1/R1 (las vacías"
    echo "dicen Coming soon). Revisar: F1 o Select abre y cierra; flechas o cruceta mueven, Enter o A empieza a"
    echo "asignar (la fila dice 'Press a key...'), Escape cancela; izquierda/derecha"
    echo "cambian Keyboard/Controller; Reset to defaults vuelve a los valores de fábrica."
    echo "Backspace y F1-F12 no se pueden asignar. Mientras está abierta el juego no"
    echo "recibe botones (sigue corriendo debajo). Se guarda en settings.ini (o \$DW3_SETTINGS)."
    echo "No termina solo, a propósito: cerrá la ventana cuando quieras."
    ;;
video2)
    keys="mode:E03,movieskip:75"
    echo "Video ACCESS DENIED (el de antes de ir al Digimundo)."
    echo "Revisar: que ocupe la pantalla con bandas negras, sin fondo azul."
    echo "Reproduce unos 5 segundos y lo salta solo (Start); termina al salir del modo 0xE03."
    ;;
pelicula)
    # The opening movie is the STDWTITL mode 0xE02 here (country_select.c:652
    # asks MODE_OPENING 0xE01 for LANGUAGE 0, MODE_OPENING_2 0xE02 otherwise;
    # this disc boots 0xE02: log "boot: mode 0xE02"). When the player finishes,
    # movie.c:264 requests the movie's next mode, 0xE00 (the title), so the
    # movie is done on the first frame that leaves 0xE02. A prefix match on
    # 0xE00 would not do: the title shares that overlay. The run plays 75 movie
    # frames (15 fps, about 5 s; the St layer's delivered-frame counter) and
    # skips with Start, which the player checks at src/menus/title/movie.c:173.
    keys="start,until:E02,movieskip:75"
    echo "Película de apertura (confirma el idioma y arranca)."
    echo "Revisar: velocidad e imagen sincronizadas con el audio."
    echo "Reproduce unos 5 segundos y la salta sola (Start); termina al volver el título."
    ;;
*)
    echo "uso: pc/review.sh batalla|resultados|cambio|libre|inicio|digilab|pelicula|video2|menu|cargar|laboratorio|tecnica|tecnicacampo|nombre|gimnasio|tienda|tiendacartas|mazo|album|cartas|controles|opciones [disco.bin]" >&2
    exit 2
    ;;
esac

make -C pc >/dev/null
# Every run keeps a detailed log (audio events included, SND_DEBUG) so an
# intermittent problem the owner hears can be compared against a good run.
logdir=pc/build/review-logs
mkdir -p "$logdir"
log="$logdir/$(date +%Y%m%d-%H%M%S)-$1.log"
echo "Registro de esta corrida: $log"
ls -1t "$logdir"/*.log 2>/dev/null | tail -n +21 | xargs -r rm -f
SND_DEBUG=1 ./pc/build/boot "$disc" 9999999 "$shot" 0 "$keys" >"$log" 2>&1
