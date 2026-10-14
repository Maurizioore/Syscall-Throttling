# Syscall Throttling — Linux Kernel Module (LKM)

![Language](https://img.shields.io/badge/Language-C%20(GNU11)-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Linux%20x86__64-orange.svg)
![Kernel](https://img.shields.io/badge/Kernel-5.4%20%7C%206.8+-success.svg)
![License](https://img.shields.io/badge/License-MIT%20-red.svg)

Modulo del kernel Linux per architettura **x86-64** che implementa un **Reference Monitor** per il *System Call Throttling*, il controllo degli accessi basato su *EUID* e *Program Name*, e il campionamento di metriche prestazionali in tempo reale.

Il progetto è stato realizzato in conformità alle [specifiche ufficiali](https://francescoquaglia.github.io/TEACHING/AOS/CURRENT/PROJECTS/project-specification-2025-2026.html) del corso di **Sistemi Operativi Avanzati** (Prof. Francesco Quaglia — Università degli Studi di Roma "Tor Vergata").

---

## Indice

1. [Portabilità e Architettura Dual-Engine Automatica](#1-portabilità-e-architettura-dual-engine-automatica)
2. [Struttura del Repository](#2-struttura-del-repository)
3. [Compilazione e Gestione del Modulo](#3-compilazione-e-gestione-del-modulo)
4. [Workflow di Validazione: I 5 Programmi User-Space](#4-workflow-di-validazione-i-5-programmi-user-space)
5. [Dettagli Implementativi e Scelte Progettuali](#5-dettagli-implementativi-e-scelte-progettuali)
6. [Interfaccia VFS e Contratto IOCTL](#6-interfaccia-vfs-e-contratto-ioctl)
7. [Analisi Sperimentale dei Tempi e Benchmark](#7-analisi-sperimentale-dei-tempi-e-benchmark)
8. [Autore](#8-autore)

---

## 1. Portabilità e Architettura Dual-Engine Automatica

Nei kernel moderni, il dispatcher delle chiamate di sistema su x86-64 è stato convertito in uno `switch-case` statico per mitigare le vulnerabilità *Spectre*, ignorando i puntatori della `sys_call_table` tradizionale. Inoltre, le moderne protezioni hardware (*Intel CET/IBT*, *CR0 Pinning* e `CONFIG_STRICT_KERNEL_RWX`) impediscono la scrittura diretta sulle pagine `.rodata`.

Per garantire la portabilità senza richiedere configurazioni manuali all'utente, il modulo seleziona **automaticamente a tempo di compilazione** (`LINUX_VERSION_CODE`) il motore di intercettazione appropriato:

* **Kernel Legacy (`Linux < 5.7` / testato su `5.4.0-generic`):** Risolve direttamente l'indirizzo di `sys_call_table` invocando l'API nativa `kallsyms_lookup_name()` (esportata nei kernel `< 5.7`) e applica il **Syscall Table Patching** disabilitando temporaneamente il bit `WP` del registro `CR0`.
* **Kernel Moderni (`Linux >= 5.11` / testato su `6.8.0-138-generic`):** Poiché `kallsyms_lookup_name` non è più esportata, utilizza una **Kprobe** per  localizzare `sys_call_table`. Dalla tabella legge l'indirizzo del dispatcher della syscall (`__x64_sys_*`) e vi aggancia il sottosistema **Ftrace**, deviando l'Instruction Pointer (`regs->ip`) verso il wrapper.

---

## 2. Struttura del Repository

La struttura del progetto è la seguente 

```text
syscall_throttling/
├── Makefile                      
├── README.md                       
├── include/
│   └── monitor_ioctl.h             # [CONTRATTO] Comandi IOCTL e struct monitor_stats
├── kmod/
│   ├── main.c                      # Init/Exit del modulo 
│   ├── setup_device.c              # Creazione automatica /dev/syscall_throttling (mode 0666)
│   ├── setup_device.h
│   ├── monitor.c                   # Liste RCU, comandi IOCTL, logica di throttling e timer callback
│   ├── monitor.h
│   ├── hooking.c                   # Dual-Engine Hooking (Ftrace / Syscall Table) e logica del  Wrapper
│   └── hooking.h
└── user/
    ├── test_all_module.c           # [1] Validazione funzionale, boundary check e verifica delle eccezioni [eseguibile sia da user che da root]
    ├── register_for_module_use.c   # [2] Registrazione target (Syscall 83, UID 1000, Program 4_launch_thread) [eseguibile solo da root]
    ├── test_get_stats.c            # [3] Lettura  metriche (IOCTL_GET_STATS) e liste  (read) [eseguibile sia da user che da root]
    └── test_multithread.c          # [4 & 5] Stress-test multithread (compilato con 2 nomi distinti) [eseguibile sia da user che da root]
```

---

## 3. Compilazione e Gestione del Modulo

### 3.1 Comandi Principali

```bash
# Compila il modulo kernel (monitor_module.ko) e i 5 eseguibili in user/
make

# Carica il modulo nel kernel (istanzia /dev/syscall_throttling)
make load

# Scarica il modulo dal kernel in sicurezza
make unload

# Rimuove tutti i file oggetto e i binari compilati
make clean
```

### 3.2 Configurazione della Politica Back-to-Back

Di default, il modulo attiva la logica **Sporadic Server Refill** per prevenire raffiche di chiamate  a cavallo tra la fine di una finestra temporale e l'inizio della successiva (il cosiddetto effetto back to back di cui soffre il deferrable server nella schedulazione dei job aperiodici nei sistemi priority driven) . È possibile disabilitare tale protezione in fase di compilazione:

```bash
# Compilazione standard (Sporadic Server ATTIVO - raccomandato)
make

# Compilazione con finestra fissa pura (consente back-to-back sui confini)
make ALLOW_BACK_TO_BACK=1
```

---

## 4. Workflow di Validazione: I 5 Programmi User-Space

Il comando `make` genera nella cartella `user/` **5 eseguibili** progettati per essere lanciati in ordine sequenziale e verificare tutte le funzionalità richieste dalla specifica.

### `1_test_all_module` — Test Suite di Robustezza ed Eccezioni
Verifica la tenuta del driver a fronte di input anomali o non autorizzati:

```bash
./user/1_test_all_module          # Test da utente non privilegiato
sudo ./user/1_test_all_module     # Test da utente root 
```

### `2_register_for_module_use` — Configurazione Operativa del Monitor
Va eseguito con privilegi di `root`. Configura l'ambiente per i test multithread successivi:
* Registra la system call target (es. `SYS_mkdir`).
* Registra l'utente standard di sistema (`UID 1000`).
* Registra il nome del quarto programma (`4_launch_thread`).
* Imposta la soglia massima `MAX` e attiva il monitor (`IOCTL_ENABLE_MONITOR`).

```bash
sudo ./user/2_register_for_module_use
```

### `3_test_get_stats_and_read` — Ispezione Statistiche (`IOCTL_GET_STATS`) e Liste (`read`)
Può essere invocato da **qualsiasi utente**. Legge dal device `/dev/syscall_throttling` per stampare le tre liste degli elementi attualmente registrati e interroga `IOCTL_GET_STATS` per mostrare il ritardo di picco, il programma/UID associato e il numero massimo e medio di thread bloccati.

```bash
./user/3_test_get_stats_and_read
```

### `4_launch_thread` — Stress Test con Nome Programma Registrato
Lancia un pool di thread concorrenti che invocano la system call monitorata. Poiché il nome dell'eseguibile (`4_launch_thread`) è **registrato nella lista dei programmi**, le chiamate vengono sottoposte a throttling **a prescindere dall'utente che avvia il processo**:

```bash
# Avviato da Utente 1000 (UID registrato + Programma registrato) -> SUBISCE THROTTLING
./user/4_launch_thread

# Avviato da Root (UID 0 NON registrato, ma Programma registrato) -> SUBISCE THROTTLING
sudo ./user/4_launch_thread
```

### `5_launch_this_as_non_registered_user` — Stress Test con Nome Programma NON Registrato
Compilato dallo **stesso identico codice sorgente** del programma 4, ma con nome eseguibile `5_launch_this_as_non_registered_user` (**non presente** nella lista dei programmi registrati). Dimostra la selettività basata sull'identificativo utente (*EUID*):

```bash
# Avviato da Utente 1000 (Programma NON registrato, ma UID 1000 registrato) -> SUBISCE THROTTLING
./user/5_launch_this_as_non_registered_user

# Avviato da Root (Programma NON registrato e UID 0 NON registrato) -> BYPASS TOTALE (Esecuzione Istantanea)
sudo ./user/5_launch_this_as_non_registered_user
```

---

## 5. Dettagli Implementativi e Scelte Progettuali

### 5.1 Prevenzione della Ricorsione in Ftrace (`within_module`)
Quando `syscall_wrapper` invoca la funzione originale `original_syscall(regs)`, il prologo della funzione del kernel contiene ancora il salto verso il trampolino di Ftrace. Per evitare un loop infinito senza ricorrere a salti manuali di offset (`address + 5`), la callback `ftrace_thunk` controlla la provenienza della chiamata tramite `parent_ip`:

```c
static void notrace ftrace_thunk(unsigned long ip, unsigned long parent_ip,
                                 struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
    struct pt_regs *regs = ftrace_get_regs(fregs);
    if (!regs)
        return;

    /* Se la chiamata proviene già dal nostro modulo (syscall_wrapper), non deviare */
    if (within_module(parent_ip, THIS_MODULE))
        return;

    regs->ip = (unsigned long)syscall_wrapper;
}
```

### 5.2 Sincronizzazione RCU, Fine-Grained Spinlocks e Cacheline Alignment
* La funzione `monitor_is_target()` viene invocata ad ogni syscall intercettata. L'attraversamento delle liste avviene dentro `rcu_read_lock()` / `rcu_read_unlock()`, garantendo letture concorrenti *wait-free* su tutti i core.
* Le tre liste dispongono di spinlock dedicati (`my_syscall_lock`, `my_uid_lock`, `my_prog_lock`) allineati alla linea di cache a 64 byte (`____cacheline_aligned`) per azzerare il *False Sharing*. Ogni inserimento verifica sotto lock l'assenza di duplicati.

```c
static spinlock_t my_syscall_lock ____cacheline_aligned = __SPIN_LOCK_UNLOCKED(my_syscall_lock);
static spinlock_t my_uid_lock     ____cacheline_aligned = __SPIN_LOCK_UNLOCKED(my_uid_lock);
static spinlock_t my_prog_lock    ____cacheline_aligned = __SPIN_LOCK_UNLOCKED(my_prog_lock);
```

### 5.3 Throttling, Risveglio Selettivo e Sporadic Server Refill
* I thread acquisiscono il diritto di esecuzione tramite `atomic_add_unless(&current_invocation, 1, READ_ONCE(max_invocation))`.
* Alla scadenza del timer, il modulo non sveglia indiscriminatamente tutta la wait queue, ma invoca `wake_up_nr(&wq1, max_invocation)`, riattivando solo il numero di thread che possono effettivamente essere serviti nella nuova finestra.
* Al primo thread che trova la quota satura e si accoda in `wq1`, `atomic_cmpxchg(&sporadic_server_refill, 0, 1)` fa ripartire la finestra del timer da quell'istante (`mod_timer`), impedendo che due raffiche ravvicinate attraversino il confine del secondo.

### 5.4 Aritmetica a Virgola Fissa per le Statistiche
Poiché l'uso della virgola mobile è vietato in kernel space, la media dei thread bloccati per secondo viene calcolata con un fattore di scala $\times 1000$:

```c
global_stats.average_thread_blocked = 
    (__u64)(global_stats.total_blocked_samples * 1000) / global_stats.total_seconds_sampled;
```

### 5.5 Safe Unload: Protocollo Stop-Drain-Teardown
Per garantire che `rmmod` non deallochi mai il codice del modulo mentre dei thread sono addormentati in `wq1` o stanno ancora eseguendo istruzioni dentro `syscall_wrapper`, il modulo traccia i thread in volo con `atomic_t active_threads`:

```c
static void __exit MonitorModuleExit(void)
{
    remove_device();                        /* Rimuove /dev/syscall_throttling */
    restore_all_registered_dispatchers();   /* Disinstalla gli hook Ftrace e ripristina I dispatcher originali (associati alla entry modificata dela syscall table)*/
    notify_module_unloading();              /* Imposta module_unloading = true  */
    cleanup_timer();                        /* Cancella il timer e fa wake_up_all(&wq1) */
    wait_for_active_threads();              /* Il thread che esegue rmmod viene messo su shutdown_wq e attende che active_threads == 0 */
    monitor_cleanup_lists();                /* Svuota le liste*/
}
```

---

## 6. Interfaccia VFS e Contratto IOCTL

Il dispositivo `/dev/syscall_throttling` espone i seguenti comandi definiti in `include/monitor_ioctl.h` (Magic Number `'M'`):

| Comando IOCTL | Tipo | Parametro | Privilegi | Descrizione |
| :--- | :---: | :--- | :---: | :--- |
| `IOCTL_SET_MAX` | `_IOW` | `unsigned int *` | `root` | Imposta il numero massimo di invocazioni per finestra ($> 0$). |
| `IOCTL_ADD_SYSCALL` | `_IOW` | `unsigned int *` | `root` | Registra e aggancia una system call. |
| `IOCTL_ADD_UID` | `_IOW` | `unsigned int *` | `root` | Registra un Effective User-ID. |
| `IOCTL_ADD_PROGRAM_NAME` | `_IOW` | `char[16]` | `root` | Registra il nome di un programma. |
| `IOCTL_REMOVE_SYSCALL` | `_IOW` | `unsigned int *` | `root` | Rimuove la syscall e ripristina il dispatcher originale. |
| `IOCTL_REMOVE_UID` | `_IOW` | `unsigned int *` | `root` | Rimuove un User-ID dalla lista. |
| `IOCTL_REMOVE_PROGRAM_NAME`| `_IOW` | `char[16]` | `root` | Rimuove un programma dalla lista. |
| `IOCTL_ENABLE_MONITOR` | `_IO` | — | `root` | Attiva il monitor e avvia il timer periodico. |
| `IOCTL_DISABLE_MONITOR` | `_IO` | — | `root` | Disattiva il monitor, ferma il timer e risveglia i thread in coda. |
| `IOCTL_GET_STATS` | `_IOR` | `struct monitor_stats *` | `tutti` | Copia nello spazio utente lo snapshot delle statistiche. |

---

## 7. Analisi Sperimentale dei Tempi e Benchmark

I test prestazionali sono stati eseguiti lanciando **120 thread concorrenti** che invocano simultaneamente la system call monitorata (`SYS_mkdir`), con soglia configurata a `MAX = 10` invocazioni per finestra temporale (1 secondo). 

Le prove mettono a confronto entrambi i motori di intercettazione (**Ftrace** su Linux `6.8` vs **Syscall Table Patching** su Linux `5.4`) e l'impatto della logica **Sporadic Server Refill** (*Back-to-Back Protection*).

| Kernel & Engine | Politica Back-to-Back | Comando Eseguito | Soggetto e Target (EUID&nbsp;/&nbsp;Programma) | Stato Throttling | Tempo&nbsp;Misurato (`real`) 
| :--- | :--- | :--- | :--- | :---: | :---: |
| **Linux&nbsp;6.8**<br>*(Ftrace)* | **Enabled**<br>*(Sporadic&nbsp;Refill)* | `sudo ./user/5_launch_this_as_non_registered_user` | `UID 0` (Non&nbsp;reg.) — `Prog 5` (Non&nbsp;reg.) | **NO** | **`53 ms`** |
| **Linux&nbsp;6.8**<br>*(Ftrace)* | **Enabled**<br>*(Sporadic&nbsp;Refill)* | `sudo ./user/4_launch_thread` | `UID 0` (Non&nbsp;reg.) — `Prog 4` (**Registrato**) | **SÌ** | **`11.30 s`** |
| **Linux&nbsp;6.8**<br>*(Ftrace)* | **Disabled**<br>*(Finestra&nbsp;Fissa)* | `sudo ./user/5_launch_this_as_non_registered_user` | `UID 0` (Non&nbsp;reg.) — `Prog 5` (Non&nbsp;reg.) | **NO** | **`53 ms`** |
| **Linux&nbsp;6.8**<br>*(Ftrace)* | **Disabled**<br>*(Finestra&nbsp;Fissa)* | `sudo ./user/4_launch_thread` | `UID 0` (Non&nbsp;reg.) — `Prog 4` (**Registrato**) | **SÌ** | **`10.67 s`** |
| **Linux&nbsp;5.4**<br>*(Syscall&nbsp;Table)* | **Enabled**<br>*(Sporadic&nbsp;Refill)* | `sudo ./user/5_launch_this_as_non_registered_user` | `UID 0` (Non&nbsp;reg.) — `Prog 5` (Non&nbsp;reg.) | **NO** | **`34 ms`** |
| **Linux&nbsp;5.4**<br>*(Syscall&nbsp;Table)* | **Enabled**<br>*(Sporadic&nbsp;Refill)* | `sudo ./user/4_launch_thread` | `UID 0` (Non&nbsp;reg.) — `Prog 4` (**Registrato**) | **SÌ** | **`11.28 s`** |
| **Linux&nbsp;5.4**<br>*(Syscall&nbsp;Table)* | **Disabled**<br>*(Finestra&nbsp;Fissa)* | `sudo ./user/5_launch_this_as_non_registered_user` | `UID 0` (Non&nbsp;reg.) — `Prog 5` (Non&nbsp;reg.) | **NO** | **`38 ms`** |
| **Linux&nbsp;5.4**<br>*(Syscall&nbsp;Table)* | **Disabled**<br>*(Finestra&nbsp;Fissa)* | `sudo ./user/4_launch_thread` | `UID 0` (Non&nbsp;reg.) — `Prog 4` (**Registrato**) | **SÌ** | **`10.42 s`** |

### Osservazioni Chiave dai Risultati Sperimentali

1. **Dimostrazione Matematica dell'Effetto Back-to-Back (120 Thread con `MAX = 10`):**
   Con 120 thread totali e una quota di 10 chiamate al secondo, i primi 10 thread passano a $T = 0\text{ s}$, mentre i restanti 110 thread richiedono esattamente $\frac{110}{10} = \mathbf{11\text{ secondi}}$ di attesa in coda.
   * Quando la protezione **Back-to-Back è abilitata** (*Sporadic Server Refill*), il tempo misurato è **`11.30 s`** (su kernel 6.8) e **`11.28 s`** (su kernel 5.4): il timer viene riallineato a 1 secondo esatto dal primo riempimento della wait queue, garantendo che gli 11 secondi teorici vengano integralmente rispettati.
   * Quando la protezione **Back-to-Back è disabilitata**, il timer continua a scorrere in modo asincrono: i primi thread colpiscono il modulo quando la finestra corrente sta già per scadere, ottenendo un secondo blocco di 10 slot dopo pochi decimi di secondo e abbassando il tempo a **`10.67 s`** e **`10.42 s`**.
2. **Confronto Overhead `Ftrace` vs `Syscall Table`:**
   Nei test in cui non vengono bloccati i 120 threads, il patching diretto della `sys_call_table` su kernel 5.4 completa in **`34–38 ms`**, mentre l'intercettazione tramite `Ftrace` su kernel 6.8 richiede **`53 ms`**. La differenza di circa `15–19 ms`  quantifica esattamente il costo del salvataggio del frame dei registri (`ftrace_regs`) e dell'invocazione di `within_module()` introdotto dal sottosistema Ftrace in cambio della piena compatibilità con i kernel moderni.

---

## 8. Autore

* **Maurizio Renzi**  
  Corso di Laurea Magistrale in Ingegneria Informatica  
  *Sistemi Operativi Avanzati (A.A. 2025/2026)*  
  Università degli Studi di Roma "Tor Vergata"
