// ============================================================================
//  Vigilante.cpp - VORTHANIA
//  Ver Vigilante.h para el por que.
// ============================================================================
#include "stdafx.h"
#include "Core/Utilities/Vigilante.h"
#include "Core/Utilities/Log/MuLogger.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
// DbgHelp es lo que sabe caminar una pila ajena; tlhelp32, lo que sabe listar
// los hilos del proceso. El pragma evita tocar el CMake por una sola libreria.
#include <dbghelp.h>
#include <tlhelp32.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace
{

using Reloj = std::chrono::steady_clock;

// Cuanto silencio del bucle principal consideramos "colgado". Un cuadro normal
// son 16 ms; una carga de mapa pesada puede tardar uno o dos segundos con el
// disco lento. Cinco segundos no lo alcanza ni el peor cambio de mapa.
constexpr long long kUmbralColgadoMs = 5000;

// Cada cuanto repetir el aviso mientras sigue colgado. Sin esto, un cuelgue de
// veinte minutos escribiria dos mil lineas iguales, que es exactamente el
// problema que este archivo viene a resolver.
constexpr long long kRepetirAvisoMs = 30000;

std::atomic<const char*>        g_etapa{"arrancando"};
std::atomic<long long>          g_ultimoLatidoBucle{0};
std::atomic<long long>          g_ultimoLatidoHelper{0};
std::atomic<unsigned long long> g_cuadros{0};
std::atomic<bool>               g_andando{false};
std::thread                     g_hilo;

#ifdef _WIN32
DWORD             g_idHiloPrincipal = 0;
DWORD             g_idHiloVigilante = 0;
std::atomic<bool> g_simbolosListos{false};
#endif

long long AhoraMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Reloj::now().time_since_epoch())
        .count();
}

#ifdef _WIN32
// Traduce una direccion a "Main.exe+0x1234". El offset se resuelve despues con
// el .pdb del build; lo importante es que el numero sea estable y comparable.
std::string Donde(const void* direccion)
{
    char texto[MAX_PATH + 64] = {};
    HMODULE modulo = nullptr;

    if (::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                 | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCSTR>(direccion), &modulo)
        && modulo != nullptr)
    {
        char ruta[MAX_PATH] = {};
        ::GetModuleFileNameA(modulo, ruta, MAX_PATH);

        const char* nombre = ruta;
        for (const char* p = ruta; *p; ++p)
        {
            if (*p == '\\' || *p == '/')
            {
                nombre = p + 1;
            }
        }

        const auto base   = reinterpret_cast<uintptr_t>(modulo);
        const auto donde  = reinterpret_cast<uintptr_t>(direccion);
        std::snprintf(texto, sizeof(texto), "%s+0x%llX", nombre,
                      static_cast<unsigned long long>(donde - base));
        return texto;
    }

    std::snprintf(texto, sizeof(texto), "0x%p", direccion);
    return texto;
}

// ============================================================================
//  Rastreo de pilas: QUE esta haciendo el hilo cuando se cuelga
//
//  Saber la etapa ("fin de cuadro") acota la zona, pero no alcanza: adentro de
//  esa zona hay presentacion de cuadro, espera de GPU y vuelta al bucle. Esto
//  suspende el hilo, le lee la pila y la escribe. La diferencia entre "se
//  cuelga en algun lado del final del cuadro" y "se cuelga adentro de
//  SDL_WaitForGPUFences" es la diferencia entre teorizar y arreglarlo.
//
//  EL ORDEN IMPORTA Y NO ES CAPRICHO
//  Mientras un hilo esta suspendido puede estar teniendo agarrado el candado
//  del heap. Si en ese momento nosotros pedimos memoria -y resolver un nombre
//  de funcion pide memoria- nos quedamos esperando un candado que solo puede
//  soltar el hilo que acabamos de congelar: el vigilante se cuelga tambien y no
//  queda nada escrito. Por eso esto va en dos tiempos: con el hilo suspendido
//  SOLO se juntan direcciones crudas en un arreglo fijo, se lo despierta, y
//  recien despues se traducen a nombres.
// ============================================================================

constexpr int kMaxMarcos = 48;

void PrepararSimbolos()
{
    // DEFERRED_LOADS para no pagar la carga de los .pdb al arrancar; UNDNAME
    // para ver "CSomething::Method" y no el nombre decorado del compilador;
    // LOAD_LINES para que salga archivo y numero de linea si el .pdb esta al lado.
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    g_simbolosListos.store(::SymInitialize(::GetCurrentProcess(), nullptr, TRUE) != FALSE,
                           std::memory_order_relaxed);
}

// Traduce una direccion a "funcion + archivo:linea" si hay .pdb, y si no al
// "Main.exe+0x1234" de siempre, que con el .pdb del build se resuelve despues.
std::string Nombrar(DWORD64 direccion)
{
    if (g_simbolosListos.load(std::memory_order_relaxed))
    {
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
        auto* simbolo = reinterpret_cast<SYMBOL_INFO*>(buffer);
        simbolo->SizeOfStruct = sizeof(SYMBOL_INFO);
        simbolo->MaxNameLen   = MAX_SYM_NAME;

        DWORD64 corrimiento = 0;
        if (::SymFromAddr(::GetCurrentProcess(), direccion, &corrimiento, simbolo))
        {
            std::string texto = simbolo->Name;
            if (corrimiento != 0)
            {
                texto += "+0x" + std::to_string(corrimiento);
            }

            IMAGEHLP_LINE64 linea = {};
            linea.SizeOfStruct = sizeof(linea);
            DWORD corrLinea = 0;
            if (::SymGetLineFromAddr64(::GetCurrentProcess(), direccion, &corrLinea, &linea)
                && linea.FileName != nullptr)
            {
                const char* archivo = linea.FileName;
                for (const char* q = linea.FileName; *q; ++q)
                {
                    if (*q == '\\' || *q == '/') archivo = q + 1;
                }
                texto += "  (";
                texto += archivo;
                texto += ":" + std::to_string(linea.LineNumber) + ")";
            }
            return texto;
        }
    }

    return Donde(reinterpret_cast<const void*>(static_cast<uintptr_t>(direccion)));
}

// Junta las direcciones de la pila de un hilo. Devuelve cuantas junto.
// El hilo queda despierto al salir, pase lo que pase.
int JuntarPila(DWORD idHilo, DWORD64 (&marcos)[kMaxMarcos])
{
    HANDLE hilo = ::OpenThread(
        THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME,
        FALSE, idHilo);
    if (hilo == nullptr)
    {
        return 0;
    }

    int cuantos = 0;

    if (::SuspendThread(hilo) != static_cast<DWORD>(-1))
    {
        CONTEXT contexto = {};
        contexto.ContextFlags = CONTEXT_FULL;

        if (::GetThreadContext(hilo, &contexto))
        {
            STACKFRAME64 marco = {};
            DWORD        maquina;
#if defined(_M_IX86)
            maquina                  = IMAGE_FILE_MACHINE_I386;
            marco.AddrPC.Offset      = contexto.Eip;
            marco.AddrFrame.Offset   = contexto.Ebp;
            marco.AddrStack.Offset   = contexto.Esp;
#elif defined(_M_X64)
            maquina                  = IMAGE_FILE_MACHINE_AMD64;
            marco.AddrPC.Offset      = contexto.Rip;
            marco.AddrFrame.Offset   = contexto.Rbp;
            marco.AddrStack.Offset   = contexto.Rsp;
#else
            maquina = 0;
#endif
            marco.AddrPC.Mode    = AddrModeFlat;
            marco.AddrFrame.Mode = AddrModeFlat;
            marco.AddrStack.Mode = AddrModeFlat;

            if (maquina != 0)
            {
                __try
                {
                    while (cuantos < kMaxMarcos
                           && ::StackWalk64(maquina, ::GetCurrentProcess(), hilo, &marco,
                                            &contexto, nullptr, ::SymFunctionTableAccess64,
                                            ::SymGetModuleBase64, nullptr)
                           && marco.AddrPC.Offset != 0)
                    {
                        marcos[cuantos++] = marco.AddrPC.Offset;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    // Caminar una pila ajena a mano puede pisar memoria que ya
                    // no es valida. Si pasa, nos quedamos con lo que juntamos:
                    // media pila dice mucho mas que ninguna.
                }
            }
        }

        ::ResumeThread(hilo);
    }

    ::CloseHandle(hilo);
    return cuantos;
}

void EscribirPila(const char* etiqueta, DWORD idHilo)
{
    DWORD64 marcos[kMaxMarcos] = {};
    const int cuantos = JuntarPila(idHilo, marcos);   // suspende y despierta

    auto registro = mu::log::Get("vigilante");
    if (cuantos == 0)
    {
        registro->error("PILA {} (hilo {}): no pude leerla", etiqueta, idHilo);
        return;
    }

    // Recien aca, con todos los hilos corriendo, se pide memoria para traducir.
    for (int i = 0; i < cuantos; ++i)
    {
        registro->error("PILA {} #{:02} {}", etiqueta, i, Nombrar(marcos[i]));
    }
}

void EscribirTodasLasPilas()
{
    auto registro = mu::log::Get("vigilante");

    EscribirPila("PRINCIPAL", g_idHiloPrincipal);

    // Los demas hilos importan porque un cuelgue de a dos -cada uno esperando
    // al otro- solo se ve mirando los dos lados.
    HANDLE foto = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (foto == INVALID_HANDLE_VALUE)
    {
        return;
    }

    THREADENTRY32 entrada = {};
    entrada.dwSize = sizeof(entrada);
    const DWORD miProceso = ::GetCurrentProcessId();

    if (::Thread32First(foto, &entrada))
    {
        do
        {
            if (entrada.th32OwnerProcessID != miProceso) continue;
            if (entrada.th32ThreadID == g_idHiloPrincipal) continue;
            if (entrada.th32ThreadID == g_idHiloVigilante) continue;  // suicidio

            char etiqueta[32] = {};
            std::snprintf(etiqueta, sizeof(etiqueta), "hilo-%lu",
                          static_cast<unsigned long>(entrada.th32ThreadID));
            EscribirPila(etiqueta, entrada.th32ThreadID);
        } while (::Thread32Next(foto, &entrada));
    }

    ::CloseHandle(foto);
    registro->flush();
}

LPTOP_LEVEL_EXCEPTION_FILTER g_filtroAnterior = nullptr;

LONG WINAPI FiltroDeExcepcion(EXCEPTION_POINTERS* info)
{
    auto registro = mu::log::Get("vigilante");

    const auto codigo = info && info->ExceptionRecord
                            ? info->ExceptionRecord->ExceptionCode
                            : 0u;
    const void* direccion = info && info->ExceptionRecord
                                ? info->ExceptionRecord->ExceptionAddress
                                : nullptr;

    const char* nombre = "desconocida";
    switch (codigo)
    {
    case EXCEPTION_ACCESS_VIOLATION:      nombre = "violacion de acceso"; break;
    case EXCEPTION_STACK_OVERFLOW:        nombre = "desborde de pila"; break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    nombre = "division por cero (entero)"; break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    nombre = "division por cero (flotante)"; break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:   nombre = "instruccion ilegal"; break;
    case EXCEPTION_IN_PAGE_ERROR:         nombre = "error de pagina"; break;
    case 0xC0000374:                      nombre = "corrupcion del heap"; break;
    default: break;
    }

    registro->error("CRASHEO: {} (0x{:08X}) en {} | etapa='{}' cuadro={}", nombre,
                    static_cast<unsigned int>(codigo), Donde(direccion),
                    g_etapa.load(std::memory_order_relaxed),
                    g_cuadros.load(std::memory_order_relaxed));

    // Una violacion de acceso trae la direccion que se quiso tocar. Un 0 ahi
    // es un puntero nulo; cualquier otra cosa suele ser memoria ya liberada.
    if (codigo == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
    {
        const auto operacion = info->ExceptionRecord->ExceptionInformation[0];
        const auto objetivo  = info->ExceptionRecord->ExceptionInformation[1];
        registro->error("CRASHEO: intento {} sobre 0x{:X}",
                        operacion == 0 ? "leer" : (operacion == 1 ? "escribir" : "ejecutar"),
                        static_cast<unsigned long long>(objetivo));
    }

    void* marcos[32] = {};
    const USHORT cuantos = ::CaptureStackBackTrace(0, 32, marcos, nullptr);
    for (USHORT i = 0; i < cuantos; ++i)
    {
        registro->error("CRASHEO:   #{:02} {}", i, Donde(marcos[i]));
    }

    registro->flush();

    // No lo atrapamos: que el proceso muera como habria muerto igual. Este
    // filtro solo esta para dejar escrito el por que.
    return g_filtroAnterior ? g_filtroAnterior(info) : EXCEPTION_CONTINUE_SEARCH;
}
#endif // _WIN32

void Rondar()
{
    auto registro = mu::log::Get("vigilante");

#ifdef _WIN32
    // Para no suspenderse a si mismo al recorrer los hilos del proceso.
    g_idHiloVigilante = ::GetCurrentThreadId();
#endif

    bool      avisado   = false;
    long long avisadoEn = 0;
    int       volcados  = 0;

    while (g_andando.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        const auto ahora  = AhoraMs();
        const auto ultimo = g_ultimoLatidoBucle.load(std::memory_order_relaxed);
        if (ultimo == 0)
        {
            continue; // todavia no empezo a latir
        }

        const auto quieto = ahora - ultimo;

        if (quieto >= kUmbralColgadoMs)
        {
            if (!avisado || (ahora - avisadoEn) >= kRepetirAvisoMs)
            {
                registro->error(
                    "COLGADO: el bucle principal no late hace {} ms | etapa='{}' cuadro={}",
                    quieto, g_etapa.load(std::memory_order_relaxed),
                    g_cuadros.load(std::memory_order_relaxed));
                registro->flush();

#ifdef _WIN32
                // Las pilas se vuelcan las dos primeras veces del episodio y no
                // mas. Una sola puede caer justo en un tramo que no dice nada;
                // dos, separadas por treinta segundos, muestran si esta quieto
                // en el mismo lugar o dando vueltas. A partir de ahi solo
                // llenarian el log con lo mismo.
                if (volcados < 2)
                {
                    ++volcados;
                    registro->error("--- pilas al momento del cuelgue ({} de 2) ---", volcados);
                    EscribirTodasLasPilas();
                }
#endif

                avisado   = true;
                avisadoEn = ahora;
            }
        }
        else if (avisado)
        {
            registro->warn("COLGADO: se destrabo solo. Volvio a latir en la etapa '{}'.",
                           g_etapa.load(std::memory_order_relaxed));
            registro->flush();
            avisado  = false;
            volcados = 0;   // el proximo episodio vuelve a tener sus dos volcados
        }
    }
}

} // namespace

namespace vorthania
{

void IniciarVigilante()
{
    if (g_andando.exchange(true))
    {
        return; // ya estaba
    }

#ifdef _WIN32
    g_filtroAnterior = ::SetUnhandledExceptionFilter(FiltroDeExcepcion);

    // Quien es "el principal" se decide aca: esta funcion se llama desde el
    // hilo que despues va a correr el bucle.
    g_idHiloPrincipal = ::GetCurrentThreadId();

    // Los simbolos se inicializan AHORA, con todo andando. Hacerlo recien
    // durante el cuelgue significaria cargar los .pdb con un hilo suspendido,
    // que es justo la forma de colgar tambien al vigilante.
    PrepararSimbolos();
#endif

    g_ultimoLatidoBucle.store(AhoraMs(), std::memory_order_relaxed);
    g_hilo = std::thread(Rondar);

    mu::log::Get("vigilante")->info(
        "VIGILANTE: activo. Avisa si el bucle principal se queda mas de {} ms sin latir.",
        kUmbralColgadoMs);
}

void DetenerVigilante()
{
    if (!g_andando.exchange(false))
    {
        return;
    }

    if (g_hilo.joinable())
    {
        g_hilo.join();
    }
}

void MarcarEtapa(const char* etapa)
{
    g_etapa.store(etapa, std::memory_order_relaxed);
}

void LatirBuclePrincipal()
{
    g_ultimoLatidoBucle.store(AhoraMs(), std::memory_order_relaxed);
    g_cuadros.fetch_add(1, std::memory_order_relaxed);
}

// Sin uso hoy: el MU Helper corre por SetTimer en el hilo principal, no en un
// hilo propio (m_timerThread se declara y nunca se arranca). Queda por si algun
// dia se lo mueve de verdad a otro hilo.
void LatirMuHelper()
{
    g_ultimoLatidoHelper.store(AhoraMs(), std::memory_order_relaxed);
}

} // namespace vorthania
