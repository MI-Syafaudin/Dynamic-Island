# Dynamic Island untuk Hyprland (Wayland Native)

Sebuah topbar bergaya **"Dynamic Island"** (terinspirasi dari Apple Dynamic Island) yang dirancang khusus untuk Linux desktop berbasis **CachyOS / Arch Linux**, **Hyprland**, dan **Wayland**.

Proyek ini dibangun dari awal dengan fokus utama pada **efisiensi ekstrem**, **konsumsi RAM ultra-rendah (<20 MB)**, **penggunaan CPU mendekati 0.0% saat idle**, serta animasi yang smooth pada hardware hemat daya seperti **AMD Ryzen 3 3200U** dengan **Radeon Vega 3 Integrated Graphics** dan RAM 4 GB.

---

## 1. Analisis Teknologi & Rationale Performa

| Aspek | Framework Tradisional (Electron / Web) | Framework Berat (Qt Quick / QML / GTK4) | **Dynamic Island (Native Wayland + Cairo + C++20)** |
| :--- | :--- | :--- | :--- |
| **Penggunaan RAM** | 300 MB – 500 MB | 60 MB – 120 MB | **~18 MB – 22 MB RSS** |
| **Penggunaan CPU (Idle)** | 3% – 12% | 0.8% – 3% | **0.0% (Kernel Poll Sleep)** |
| **Beban GPU (Vega 3)** | Tinggi (Chromium Compositor) | Sedang (OpenGL/Vulkan shaders) | **Nol / Sangat Ringan (Wayland SHM Direct)** |
| **Startup Time** | 1.5s – 3.0s | 500ms – 1.0s | **< 5 milidetik (Instant)** |
| **Ukuran Binary** | 150 MB+ | 30 MB+ | **378 KB (Bisa di-strip ke ~180 KB)** |
| **Wayland Layer Shell** | Tidak native / hacky | Perlu plugin eksternal | **Native Protocol (`zwlr_layer_shell_v1` Overlay)** |

### Mengapa Sangat Ringan?
1. **Zero Browser Engine**: Tidak ada Node.js, V8, Chromium, maupun WebKit.
2. **Direct Wayland Layer Shell**: Menggunakan protokol native `zwlr_layer_shell_v1` dari wlroots/Hyprland. Jendela diletakkan di layer `OVERLAY` dengan anchor di tengah atas (`ANCHOR_TOP`), margin atas dapat disesuaikan, dan `exclusive_zone = 0` sehingga bar mengambang bebas. Dynamic Island tetap tampak di atas jendela biasa maupun video full screen (baik pemutar video lokal seperti mpv/VLC maupun streaming browser seperti YouTube).
3. **Double-Buffered Cairo Graphics**: Menggambar antialiased pill, sudut membulat, dan tipografi subpixel langsung ke shared-memory buffer (`wl_shm`). Compositor Hyprland membacanya secara zero-copy.
4. **Sinkronisasi Frame 60 Hz**: Animasi di-drive oleh callback VSync Wayland (`wl_surface_frame`), bukan `usleep` atau loop timer yang membebani CPU. Begitu animasi selesai, registrasi frame dihentikan total sehingga CPU langsung tidur (*zero wakeups*).
5. **Event-Driven IPC**:
   - Status workspace, fokus window, dan fullscreen didapat langsung secara real-time melalui push event dari Hyprland socket2 (`$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock`).
   - Tidak ada polling terus-menerus terhadap `hyprctl`.
   - Modul system monitor (CPU/RAM/GPU) bersifat **lazy**: tidak membaca `/proc/stat` saat idle, dan hanya membaca data setiap 1.5 detik saat mode `Expanded_System` sedang dibuka oleh user.
6. **Adaptive Dynamic Width**: Lebar Dynamic Island saat idle/compact tidak kaku/statis, melainkan menghitung kebutuhan ruang modul secara dinamis menggunakan perekaman layout Pango-Cairo dan menganimasikannya secara halus (cubic ease-out 60 FPS). Status program aktif maupun modul lain tidak akan terpotong setengah.

---

## 2. Struktur Proyek

```
dynamic_island/
├── Makefile                        # Build system (g++ C++20)
├── install.sh                      # Skrip instalasi otomatis untuk CachyOS/Arch
├── uninstall.sh                    # Skrip uninstaller bersih
├── README.md                       # Dokumentasi lengkap
├── build/
│   └── dynamic-island              # Binary executable mandiri (~378 KB)
├── config/
│   └── config.json                 # File konfigurasi utama
├── themes/
│   ├── default_dark.json           # Tema Apple dark (#111111)
│   ├── midnight_blue.json          # Tema laut dalam (#0b0f19)
│   ├── emerald_dark.json           # Tema emerald gelap (#0b1411)
│   └── amoled_black.json           # Tema hitam pekat AMOLED (#000000)
├── scripts/
│   ├── volume_notify.sh            # Wrapper volume keybinding
│   ├── brightness_notify.sh        # Wrapper brightness keybinding
│   └── dynamic-island-ctl.sh       # Controller utilitas CLI
├── hyprland/
│   └── dynamic-island.conf         # Konfigurasi autostart & keybinding Hyprland
└── src/
    ├── main.cpp                    # CLI entry & daemon lifecycle
    ├── app.hpp / app.cpp           # State machine, animasi & event loop
    ├── config.hpp / config.cpp     # Parser konfigurasi JSON
    ├── wayland.hpp / wayland.cpp   # Wrapper Wayland Layer Shell & input
    ├── renderer.hpp / renderer.cpp # Rendering Cairo & Pango
    ├── hyprland_ipc.hpp / .cpp     # Listener non-blocking socket2 Hyprland
    ├── ipc_server.hpp / .cpp       # Server socket UNIX lokal untuk CLI
    ├── json.hpp                    # Parser JSON ringan tanpa dependensi
    ├── protocols/
    │   ├── wlr-layer-shell-unstable-v1.xml
    │   ├── wlr-layer-shell-protocol.h / .c
    │   └── xdg-shell-protocol.h / .c
    └── modules/
        ├── module_base.hpp         # Interface modular & hit box
        ├── clock_module.hpp / .cpp
        ├── workspace_module.hpp / .cpp
        ├── window_module.hpp / .cpp
        ├── audio_module.hpp / .cpp
        ├── media_module.hpp / .cpp
        ├── battery_module.hpp / .cpp
        ├── network_module.hpp / .cpp
        ├── system_module.hpp / .cpp
        ├── screenshot_module.hpp / .cpp
        ├── notification_module.hpp / .cpp
        └── quickaction_module.hpp / .cpp
```

---

## 3. Fitur Utama

| Fitur | Deskripsi | Aksi Interaktif |
| :--- | :--- | :--- |
| **1. Clock** | Jam & Menit (HH:MM). | Klik untuk ekspansi: Jam digital besar, Hari, Tanggal lengkap, dan Uptime sistem. |
| **2. Workspace Hyprland** | Menampilkan workspace aktif (1, 2, [3], 4, 5). Workspace saat ini disorot dengan pill accent. | Klik pada nomor workspace untuk berpindah langsung (`hyprctl dispatch workspace N`). Update realtime via socket2. |
| **3. Active Window** | Menampilkan class/nama window aktif (misal `kitty`, `google-chrome`, `Visual Studio Code`). Lebar Dynamic Island menyesuaikan panjang nama/status program aktif secara otomatis (adaptive width) sehingga teks tidak terpotong setengah. | Terhubung dengan event `activewindow>>` Hyprland socket2. |
| **4. Audio Control** | Terhubung ke PipeWire / WirePlumber via `wpctl`. Menampilkan persentase & status mute. | Otomatis ekspansi saat volume berubah. Scroll mouse pada island untuk atur volume. Klik untuk toggle mute. |
| **5. Media Player** | Integrasi MPRIS (`playerctl`). Menampilkan judul lagu yang sedang diputar. | Klik untuk ekspansi: Info artis & tombol kendali `[⏮ Prev]`, `[⏯ Play/Pause]`, `[⏭ Next]`. |
| **6. Battery Status** | Membaca sysfs laptop (`/sys/class/power_supply/BAT*`). Menampilkan icon petir saat charging. | Indikator visual berubah merah saat baterai ≤ 20%. |
| **7. Network Status** | Mendeteksi WiFi (dengan nama SSID) / Ethernet / Terputus via `nmcli` & sysfs. | Klik untuk ekspansi: Interface name & IP lokal. |
| **8. System Monitor** | Menampilkan CPU %, RAM %, dan GPU % (Radeon Vega 3). | Klik untuk ekspansi: 3 progress bar horizontal halus + Temperatur CPU (°C) & detail penggunaan memori (GB). |
| **9. Screenshot Tool** | Terintegrasi dengan `grim` + `slurp` + `wl-copy`. | Simpan otomatis ke `~/Pictures/Screenshots/`, salin ke clipboard, dan memunculkan banner "📸 Screenshot Captured!". |
| **10. Notifications** | Banner notifikasi mandiri tanpa daemon berat. | Menerima notifikasi via `dynamic-island notify "App" "Message"` dan otomatis collapse setelah 4.5 detik. |
| **11. Quick Actions** | Panel kontrol cepat saat klik kanan atau shortcut. | Tombol pill: `[WiFi]`, `[Bluetooth]`, `[Mute]`, `[Night Light]`, `[Screenshot]`, `[Power]`. |

---

## 4. Cara Instalasi

### Prasyarat di CachyOS / Arch Linux
Semua dependensi dasar sudah tersedia di sistem CachyOS standar:
```bash
sudo pacman -S --needed gcc make pkgconf wayland cairo pango playerctl wireplumber grim slurp wl-clipboard brightnessctl
```

### Menjalankan Installer
Cukup jalankan skrip `install.sh` di dalam direktori proyek:
```bash
cd ~/Projects/dynamic_island
./install.sh
```

Skrip ini akan secara otomatis:
1. Memverifikasi display Wayland dan compositor Hyprland.
2. Mengompilasi source code menggunakan `make -j$(nproc)`.
3. Memasang binary ke `~/.local/bin/dynamic-island`.
4. Membuat konfigurasi default di `~/.config/dynamic-island/config.json`.
5. Memasang tema di `~/.config/dynamic-island/themes/`.
6. Membuat file `~/.config/hypr/dynamic-island.conf`.
7. Membuat backup otomatis `~/.config/hypr/hyprland.conf` sebelum menambahkan `source = ~/.config/hypr/dynamic-island.conf`.
8. Menyiapkan systemd user service `~/.config/systemd/user/dynamic-island.service`.

---

## 5. Cara Menjalankan & Autostart

### 1. Menjalankan Langsung via Terminal
```bash
dynamic-island &
```

### 2. Autostart via Hyprland (Rekomendasi)
File `~/.config/hypr/dynamic-island.conf` sudah menyertakan baris:
```ini
exec-once = dynamic-island
```
Karena baris `source = ~/.config/hypr/dynamic-island.conf` telah ditambahkan ke `hyprland.conf`, Dynamic Island akan otomatis berjalan setiap kali Hyprland dimulai.

### 3. Autostart via Systemd User Service (Opsional)
Jika Anda lebih memilih manajemen service systemd:
```bash
systemctl --user daemon-reload
systemctl --user enable --now dynamic-island.service
```

---

## 6. Konfigurasi (`config.json`)

Konfigurasi disimpan di `~/.config/dynamic-island/config.json`. Setiap perubahan dapat diterapkan secara langsung tanpa kompilasi ulang dengan perintah:
```bash
dynamic-island reload
```

### Contoh Konfigurasi:
```json
{
  "position": "top-center",
  "margin_top": 8,
  "idle_width": 260,
  "idle_height": 34,
  "corner_radius": 17,
  "opacity": 0.92,
  "animation_duration_ms": 220,
  "font_family": "FiraCode Nerd Font, JetBrainsMono Nerd Font, Sans",
  "font_size": 11,
  "layer": "overlay",
  "hide_on_fullscreen": false,
  "adaptive_width": true,
  "max_idle_width": 850,
  "max_window_title_length": 28,
  "colors": {
    "background": "#111111",
    "border": "#2c2c2e",
    "text": "#ffffff",
    "subtext": "#8e8e93",
    "accent": "#38ef7d",
    "accent_blue": "#0a84ff",
    "warning": "#ffd60a",
    "danger": "#ff453a",
    "card_bg": "#1c1c1e"
  },
  "modules": {
    "clock": true,
    "workspace": true,
    "active_window": true,
    "audio": true,
    "media": true,
    "battery": true,
    "network": true,
    "system": true,
    "screenshot": true,
    "notification": true,
    "quickaction": true
  },
  "timeouts": {
    "volume_ms": 2000,
    "brightness_ms": 2000,
    "notification_ms": 4500,
    "screenshot_ms": 2800,
    "auto_collapse_ms": 5000
  }
}
```

### Opsi Konfigurasi Penting:
| Parameter | Default | Keterangan |
| :--- | :--- | :--- |
| `layer` | `"overlay"` | Layer Wayland Layer Shell (`"overlay"` atau `"top"`). Layer `overlay` menjamin Dynamic Island tetap mengambang di atas video fullscreen maupun game. |
| `hide_on_fullscreen` | `false` | Menentukan apakah Dynamic Island disembunyikan saat aplikasi fullscreen aktif. Default `false` agar tetap tampak saat menonton video lokal (mpv/vlc) atau streaming browser (YouTube/Netflix). Ubah ke `true` jika ingin menyembunyikan bar saat fullscreen. |
| `adaptive_width` | `true` | Otomatis mengukur dan menyesuaikan lebar Dynamic Island secara dinamis mengikuti status dan nama program/jendela aktif tanpa terpotong setengah. |
| `idle_width` | `260` | Lebar baseline (minimal) Dynamic Island saat mode idle. |
| `max_idle_width` | `850` | Batas maksimum lebar ekspansi dinamis saat mode idle agar tidak memenuhi layar. |
| `max_window_title_length` | `28` | Panjang maksimum karakter judul/class window aktif sebelum dipotong dengan `..`. |


---

## 7. Navigasi Shortcut & Kontrol Mouse

### Mouse
- **Left Click**:
  - Klik angka Workspace: Pindah workspace Hyprland.
  - Klik Jam: Buka ekspansi kalender & uptime.
  - Klik Lagu: Buka kontrol pemutar media.
  - Klik Tombol Media `[⏮ ⏯ ⏭]`: Kontrol musik via MPRIS.
  - Klik Tombol Quick Action: Toggle WiFi, Bluetooth, Night Light, Mute, Screenshot, Power.
  - Klik di luar tombol saat ekspansi: Otomatis collapse kembali ke idle.
- **Right Click**: Membuka / menutup menu Quick Actions.
- **Scroll Wheel**: Mengatur volume naik/turun dengan step 5%.

### Shortcut Keyboard (Hyprland Keybindings)
| Shortcut | Aksi |
| :--- | :--- |
| `SUPER + I` | Toggle Dynamic Island (Ekspansi / Collapse) |
| `SUPER + V` | Buka popup kontrol Volume |
| `SUPER + M` | Buka pemutar media (Now Playing & controls) |
| `SUPER + S` | Buka System Monitor (CPU, RAM, GPU Vega 3, Temp) |
| `SUPER + C` | Buka Kalender & Waktu Lengkap |
| `SUPER + N` | Buka Status Jaringan |
| `Print` | Ambil screenshot layar penuh |
| `Shift + Print` | Ambil screenshot area pilihan (`slurp`) |
| `Tombol Volume (+/-)` | Naik/turun volume audio (otomatis popup island) |
| `Tombol Mute` | Mute/unmute audio |
| `Tombol Brightness (+/-)` | Naik/turun kecerahan layar (otomatis popup island) |
| `Tombol Play / Next / Prev` | Kendali playback MPRIS |

---

## 8. CLI & IPC Control

Binary `dynamic-island` berfungsi ganda sebagai daemon dan kontroler CLI:
```bash
# Toggle expand / collapse
dynamic-island toggle

# Force collapse ke mode pill normal
dynamic-island collapse

# Buka modul tertentu
dynamic-island expand system
dynamic-island expand media
dynamic-island expand audio
dynamic-island expand clock
dynamic-island expand quick

# Mengubah volume dan memicu ekspansi
dynamic-island volume +5%
dynamic-island volume -5%
dynamic-island volume toggle

# Mengubah brightness
dynamic-island brightness +5%
dynamic-island brightness -5%

# Kendali media
dynamic-island media play-pause
dynamic-island media next
dynamic-island media previous

# Mengambil screenshot
dynamic-island screenshot full
dynamic-island screenshot area

# Mengirim pesan notifikasi ke island
dynamic-island notify "Kitty" "Build berhasil diselesaikan!"

# Reload konfigurasi saat runtime
dynamic-island reload

# Menutup daemon
dynamic-island quit
```

---

## 9. Troubleshooting & Debugging

### 1. Memeriksa Apakah Daemon Sedang Berjalan
```bash
ps aux | grep dynamic-island
```

### 2. Menguji Komunikasi Socket Hyprland
Pastikan environment variable Hyprland aktif:
```bash
echo $HYPRLAND_INSTANCE_SIGNATURE
ls -la $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock
```

### 3. Menjalankan dengan Debugging Wayland
Untuk melihat lalu lintas protokol Wayland:
```bash
WAYLAND_DEBUG=1 dynamic-island
```

### 4. Font Icon / Karakter Kotak-kotak
Jika icon tidak muncul dengan benar, pastikan Nerd Font telah terpasang:
```bash
fc-list : family | grep -i nerd
```
Jika belum, pasang via pacman di CachyOS:
```bash
sudo pacman -S ttf-firacode-nerd
```

---

## 10. Cara Uninstall Bersih

Jika Anda ingin mencopot Dynamic Island:
```bash
cd ~/Projects/dynamic_island
./uninstall.sh
```
Skrip uninstaller akan menghentikan proses, menghapus binary dari `~/.local/bin`, membersihkan baris konfigurasi dari `hyprland.conf`, dan menonaktifkan systemd service tanpa menyisakan berkas sampah.
