#include "cli/messages.hpp"

#include <cstddef>

namespace revisa::cli {

namespace {

struct Entry {
    Msg id;
    std::string_view text;
};

// One row per Msg, in enum order (checked below).
constexpr Entry kTable[] = {
    {Msg::Help,
     "Uso: revisa check [FICHERO…] [--json] [--no-guards] [--no-spec] [--model RUTA] [--threads N]\n"
     "     revisa check --jsonl [--no-guards] [--no-spec] [--timings] [--model RUTA] [--threads N]\n"
     "                  < casos.jsonl\n"
     "     revisa check --jsonl --replay [--no-guards] < correcciones.jsonl\n"
     "     revisa diff [--json] ORIGINAL CORREGIDO\n"
     "     revisa diff --jsonl < pares.jsonl\n"
     "     revisa classify --jsonl < ediciones.jsonl\n"
     "     revisa segment --jsonl < textos.jsonl\n"
     "     revisa serve [--port N] [--no-spec] [--model RUTA] [--threads N]\n"
     "     revisa lsp [--no-spec] [--model RUTA] [--threads N]\n"
     "     revisa token new NOMBRE | list | revoke NOMBRE\n"
     "     revisa --print-prompt TEXTO\n"
     "     revisa --help | --version\n"
     "\n"
     "Corrige la ortografía y la gramática de textos en castellano, en local.\n"
     "\n"
     "Órdenes:\n"
     "  check                  corrige los ficheros (o la entrada estándar) y escribe el texto\n"
     "                         corregido; con varios ficheros, cada uno va tras «==> FICHERO <==»\n"
     "  diff                   las ediciones que llevan de ORIGINAL a CORREGIDO, con su tipo y su\n"
     "                         explicación (sin modelo ni guardas); posiciones en unidades UTF-16\n"
     "  classify               el tipo y la explicación de cada edición (sin modelo)\n"
     "  segment                los segmentos que vería el modelo y por qué se saltan (sin modelo)\n"
     "  serve                  servidor HTTP local en 127.0.0.1 (para integraciones propias),\n"
     "                         con token; los tokens se crean con «revisa token new»\n"
     "  lsp                    servidor de lenguaje (LSP) por la entrada y la salida estándar\n"
     "  token                  crea, lista o revoca los tokens del servidor\n"
     "\n"
     "Opciones:\n"
     "  --json                 check y diff: un objeto JSON por entrada, en una línea\n"
     "  --jsonl                check: lee {\"text\", \"id\"} por línea de la entrada estándar y escribe\n"
     "                         {\"id\", \"corrected\", \"edits\", \"segments_skipped\", \"changed\"}, una\n"
     "                         línea por caso nada más corregirlo, con el modelo cargado una vez;\n"
     "                         diff: lee {\"original\", \"corrected\", \"id\"} y escribe {\"id\", \"edits\"},\n"
     "                         con \"out_of_catalog\" en cada edición; classify: lee {\"from\", \"to\",\n"
     "                         \"left\", \"right\", \"id\"} y escribe {\"id\", \"type\", \"explanation\"};\n"
     "                         segment: lee {\"text\", \"id\"} y escribe {\"id\", \"segments\"}, cada uno\n"
     "                         {\"text\", \"skip\"}, con skip \"\", \"long\", \"protected\" o \"special\"\n"
     "  --replay               check --jsonl: lee {\"text\", \"corrected\", \"id\"}, con la corrección de\n"
     "                         otro sistema, y la pasa por el mismo recorrido y las mismas guardas, sin\n"
     "                         modelo; añade \"replay_dropped\", las ediciones que no caben en un segmento\n"
     "  --no-guards            check --json y --jsonl: sin las guardas ni los tipos desactivados\n"
     "  --no-spec              check, serve y lsp: sin la decodificación especulativa (más lento;\n"
     "                         para depurar)\n"
     "  --timings              check --jsonl: añade \"timings\", el tiempo del modelo en cada segmento\n"
     "  --port N               serve: el puerto (por defecto, 47312 o la clave port)\n"
     "  --model RUTA           fichero GGUF del modelo (por defecto, el descargado)\n"
     "  --threads N            hilos de inferencia, de 1 a 256 (por defecto, los núcleos de rendimiento\n"
     "                         en las CPU híbridas y todos los físicos en las demás)\n"
     "  --print-prompt TEXTO   escribe la cadena exacta que ve el modelo y sale\n"
     "  -h, --help             muestra esta ayuda\n"
     "  --version              muestra la versión de revisa y la del contrato del modelo\n"
     "\n"
     "Configuración: ~/.config/revisa/config (en Windows, %APPDATA%\\revisa\\config), con las claves\n"
     "model, threads, disabled_types y port (por ejemplo, «disabled_types = MAYUSCULA, ESPACIADO»).\n"
     "\n"
     "Códigos de salida: 0 sin cambios, 1 con cambios, 2 error, 130 interrumpido. Con --jsonl,\n"
     "0 si todas las líneas se han procesado; ante una línea no válida, 2 (las anteriores ya\n"
     "se han escrito en check, ninguna en diff, classify y segment).\n"},
    {Msg::Version, "revisa {} (contrato del modelo v{})\n"},
    {Msg::UsageHint, "Prueba «revisa --help».\n"},
    {Msg::NoCommand, "revisa: falta la orden (por ejemplo, «revisa check FICHERO»).\n"},
    {Msg::UnknownCommand, "revisa: orden desconocida: {}\n"},
    {Msg::UnknownFlag, "revisa: opción desconocida: {}\n"},
    {Msg::MissingValue, "revisa: falta el valor de {}\n"},
    {Msg::BadThreads, "revisa: valor no válido para --threads: «{}» (tiene que ser un número de 1 a {})\n"},
    {Msg::PrintPromptExtra, "revisa: --print-prompt no admite más argumentos: {}\n"},
    {Msg::FlagNotForCommand, "revisa: la opción {} no sirve con «revisa {}»\n"},
    {Msg::DiffArgs, "revisa: «revisa diff» necesita dos textos, ORIGINAL y CORREGIDO (o --jsonl, sin textos)\n"},
    {Msg::ClassifyArgs, "revisa: «revisa classify» solo lee ediciones por la entrada estándar, con --jsonl\n"},
    {Msg::Config, "revisa: error en la configuración: {}\n"},
    {Msg::ReadFile, "revisa: no se puede leer {}: {}\n"},
    {Msg::ReadStdin, "revisa: no se puede leer la entrada estándar: {}\n"},
    {Msg::FileHeader, "==> {} <==\n"},
    {Msg::ModelMissing, "revisa: no existe el fichero del modelo: {}\n"},
    {Msg::NoModelsDir,
     "revisa: no se sabe dónde guardar el modelo (no están definidas ni XDG_DATA_HOME ni HOME). "
     "Indica uno con --model.\n"},
    {Msg::DownloadPrompt, "El modelo ({}, {} MB) no está descargado. ¿Descargarlo ahora desde {}? [S/n] "},
    {Msg::DownloadNoTty,
     "revisa: el modelo ({}, {} MB) no está descargado. Ejecuta revisa en un terminal para descargarlo, "
     "o descárgalo de {} y pásalo con --model.\n"},
    {Msg::DownloadDeclined, "revisa: no se ha descargado el modelo.\n"},
    {Msg::DownloadDone, "Modelo descargado en {}\n"},
    {Msg::DownloadFailed, "revisa: {}\n"},
    {Msg::LoadFailed, "revisa: no se pudo cargar el modelo ({})\n"},
    {Msg::DecodeFailed, "revisa: falló la inferencia del modelo ({})\n"},
    {Msg::EngineError, "revisa: error del motor: {} ({})\n"},
    {Msg::DiffNone, "Sin diferencias.\n"},
    {Msg::EditLine, "{}-{} «{}» → «{}»  {}: {}\n"},
    {Msg::EditLineOther, "{}-{} «{}» → «{}»  OTRO: posible corrección\n"},
    {Msg::JsonlLine, "revisa: línea {} de la entrada: {}\n"},
    {Msg::JsonlInvalid, "JSON no válido ({})"},
    {Msg::JsonlNotObject, "tiene que ser un objeto JSON"},
    {Msg::JsonlField, "falta el campo «{}» o no es un texto"},
    {Msg::FlagNeeds, "revisa: la opción {} solo sirve con {}\n"},
    {Msg::CheckJsonlFiles, "revisa: «revisa check --jsonl» lee los casos por la entrada estándar, sin ficheros\n"},
    {Msg::SegmentArgs, "revisa: «revisa segment» solo lee textos por la entrada estándar, con --jsonl\n"},
    {Msg::BadPort, "revisa: valor no válido para --port: «{}» (tiene que ser un número de 1 a 65535)\n"},
    {Msg::NoOperands, "revisa: «revisa {}» no admite argumentos: {}\n"},
    {Msg::TokenArgs, "revisa: uso: «revisa token new NOMBRE», «revisa token list» o «revisa token revoke NOMBRE»\n"},
    {Msg::TokensNoPath,
     "revisa: no se sabe dónde guardar los tokens (no están definidas ni XDG_CONFIG_HOME ni HOME)\n"},
    {Msg::TokensFile, "revisa: fichero de tokens: {}\n"},
    {Msg::TokenNone, "No hay tokens. Crea uno con «revisa token new NOMBRE».\n"},
    {Msg::TokenListLine, "{}  creado {}\n"},
    {Msg::TokenCreated, "revisa: token «{}» creado. Guárdalo ahora: no se vuelve a mostrar.\n"},
    {Msg::TokenRevoked, "revisa: token «{}» revocado.\n"},
    {Msg::TokenBadName,
     "revisa: nombre de token no válido (de 1 a 64 letras sin tilde, cifras, «.», «_» o «-»)\n"},
    {Msg::TokenDuplicate, "revisa: ya existe un token llamado «{}»\n"},
    {Msg::TokenNotFound, "revisa: no existe ningún token llamado «{}»\n"},
    {Msg::ServeListening, "revisa: escuchando en http://127.0.0.1:{}\n"},
    {Msg::ServeNoTokens,
     "revisa: todavía no hay ningún token; crea uno con «revisa token new NOMBRE» para conectar un cliente.\n"},
    {Msg::ServeBindFailed, "revisa: no se puede escuchar en 127.0.0.1:{} (¿hay otro programa usando ese puerto?)\n"},
};

static_assert(std::size(kTable) == static_cast<std::size_t>(Msg::Count_), "one row per Msg");

constexpr bool in_order() {
    for (std::size_t i = 0; i < std::size(kTable); ++i)
        if (static_cast<std::size_t>(kTable[i].id) != i) return false;
    return true;
}
static_assert(in_order(), "kTable rows must follow the Msg order");

}  // namespace

std::string_view msg(Msg m) { return kTable[static_cast<std::size_t>(m)].text; }

std::string fill(std::string_view pattern, std::initializer_list<std::string_view> values) {
    std::string out;
    auto next = values.begin();
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 1 < pattern.size() && pattern[i + 1] == '}') {
            if (next != values.end()) out += *next++;
            ++i;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

std::string msg(Msg m, std::initializer_list<std::string_view> values) { return fill(msg(m), values); }

}  // namespace revisa::cli
