# SlideCapture untuk Resolume

Dua plugin source FFGL untuk Resolume Avenue/Arena **7.8 ke atas**, Windows dan macOS.

| Plugin | Nama di Resolume | Fungsi |
|---|---|---|
| `SCScreenCapture` | **SC Screen Capture** | Capture satu monitor atau satu jendela aplikasi, cursor bisa ditampilkan atau disembunyikan |
| `SCPresentation` | **SC Presentation** | PPT/PPTX (animasi ikut jalan), PDF, dan Google Slides |

Kedua plugin memakai mesin capture yang sama. Plugin dipisah supaya panel parameter tetap rapi, dan supaya kalau PowerPoint macet, screen capture tetap jalan.

---

## Persyaratan

| | Windows | macOS |
|---|---|---|
| OS | Windows 10 versi 1903+ (opsi cursor butuh 2004+; Windows 11 tanpa bingkai kuning) | macOS 12.3+ |
| Resolume | 7.8+ | 7.8+ (7.11+ untuk Apple Silicon native, plugin sudah universal) |
| Animasi PPT | Microsoft PowerPoint (desktop) | Microsoft PowerPoint for Mac |
| PPT tanpa animasi | PowerPoint **atau** LibreOffice (gratis) | PowerPoint **atau** LibreOffice |
| PDF, Google Slides | Tidak perlu aplikasi lain | Tidak perlu aplikasi lain |

---

## Build

### Cara termudah: GitHub Actions (tanpa install apa pun)
Setiap push otomatis membangun plugin dan installer, lalu menguji pemasangannya.
Buka tab **Actions** → run terbaru → unduh artifact **SlideCapture-Windows** (berisi installer `.exe`) dan **SlideCapture-macOS** (berisi installer `.pkg`).

### Windows (manual)
Butuh Visual Studio 2022 (workload *Desktop development with C++*, termasuk Windows SDK 10.0.22621 atau lebih baru) dan CMake 3.21+.
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```
```bash
cmake --build build --config Release
```
Hasilnya ada di `build\Release\SCScreenCapture.dll` dan `build\Release\SCPresentation.dll`.

### macOS (manual)
Butuh Xcode Command Line Tools dan CMake.
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```
```bash
cmake --build build
```
Hasilnya `build/SCScreenCapture.bundle` dan `build/SCPresentation.bundle` (universal arm64 + x86_64).

> CMake mengunduh FFGL SDK resmi Resolume secara otomatis. Kalau offline, clone `github.com/resolume/ffgl` sendiri lalu tambahkan `-DFFGL_SDK_DIR=path/ke/ffgl`.

---

## Instalasi

### Pakai installer (disarankan)
Unduh installer dari halaman **Releases** repo ini, atau dari **Actions → run terbaru → Artifacts**.

**Windows: `SlideCapture-Setup-Windows.exe`**
1. Tutup Resolume.
2. Jalankan installer. Tidak perlu hak admin.
   Karena installer belum ditandatangani, Windows SmartScreen bisa memblokirnya: klik **More info → Run anyway**.
3. Plugin dipasang ke `Documents\Resolume Arena\Extra Effects` dan/atau `Documents\Resolume Avenue\Extra Effects`, sesuai Resolume yang terpasang.
4. Buka Resolume. Plugin ada di tab **Sources**: **SC Screen Capture** dan **SC Presentation**.
5. Uninstall lewat **Settings → Apps → SlideCapture for Resolume**.

**macOS: `SlideCapture-Installer-macOS.pkg`**
1. Tutup Resolume.
2. Pertama kali: **klik kanan → Open**, karena installer belum di-notarize Apple. Kalau masih diblokir, buka **System Settings → Privacy & Security** lalu klik **Open Anyway**.
3. Plugin dipasang ke `~/Documents/Resolume Arena/Extra Effects` dan/atau `Resolume Avenue`. Tanda karantina dilepas otomatis.
4. Buka Resolume. Izinkan Resolume di **System Settings → Privacy & Security → Screen Recording**, lalu restart Resolume sekali.
5. Uninstall: jalankan `/Library/Application Support/SlideCapture/Uninstall SlideCapture.command` (klik dua kali).

### Manual
Salin file `.dll` (Windows) atau `.bundle` (macOS) ke folder Extra Effects. Folder ini juga bisa diatur di **Preferences → Video → Extra Effects**. Lalu restart Resolume.

**Catatan macOS:**
- Untuk mode Animated, macOS akan bertanya *"Resolume wants to control Microsoft PowerPoint"*. Klik **OK**. PowerPoint juga bisa meminta izin akses file (*Grant Access*).
- Instalasi manual dari file unduhan perlu melepas karantina:
  ```bash
  xattr -dr com.apple.quarantine ~/Documents/Resolume\ Arena/Extra\ Effects/SC*.bundle
  ```

### Membuat rilis baru
Push tag versi, misalnya `v1.0.0`. CI membuat GitHub Release berisi kedua installer:
```bash
git tag v1.0.0
```
```bash
git push origin v1.0.0
```

---

## SC Screen Capture

| Parameter | Fungsi |
|---|---|
| **Source** | `Monitor` atau `Window` |
| **Monitor** | Daftar monitor |
| **Window** | Daftar jendela aplikasi yang sedang terbuka. Resolume sendiri tidak masuk daftar, supaya tidak terjadi feedback loop |
| **Refresh List** | Perbarui daftar monitor/jendela, misalnya setelah membuka aplikasi baru |
| **Show Cursor** | Tampilkan atau sembunyikan pointer mouse |
| **Hide Title Bar** | (Windows) Buang title bar dan border jendela |
| **Fit** | `Fit` (seluruh gambar terlihat), `Fill` (penuh, sebagian terpotong), `Stretch` |
| **Crop Left/Right/Top/Bottom** | Potong tepi. Nilai 1.0 = 50% |
| **Status** | Info atau pesan error |

Catatan:
- Jendela yang dipilih diingat lewat nama aplikasi + judulnya, jadi tetap ketemu saat composition dibuka lagi. Kalau judulnya berubah (misalnya tab browser), plugin memakai jendela pertama dari aplikasi yang sama.
- Jendela tetap ter-capture walaupun tertutup jendela lain, **asal tidak di-minimize**.

---

## SC Presentation

| Parameter | Fungsi |
|---|---|
| **Source** | `File (PPT/PDF)` atau `Google Slides` |
| **File** | Pilih `.pptx .ppt .ppsx .pps .pptm .potx .odp .pdf` |
| **Google Slides URL** | Tempel link share Google Slides |
| **Mode** | Lihat tabel di bawah |
| **Previous / Next** | Mundur/maju **satu langkah**: satu animasi atau satu slide. Bisa di-mapping ke MIDI, OSC, atau keyboard |
| **First Slide** | Kembali ke slide 1 |
| **Slide** | Lompat ke nomor slide. Nilainya juga ikut berubah saat Next/Prev ditekan |
| **Reload** | Muat ulang. Untuk Google Slides, unduh ulang versi terbaru |
| **Options → Static Converter** | `Auto` / `PowerPoint` / `LibreOffice` untuk mode Static |
| **Options → Loop** | Setelah slide terakhir, kembali ke slide pertama |
| **Options → Hide PowerPoint Window** | (Windows) Jendela slideshow dipindah ke luar layar |
| **Fit, Crop** | Sama seperti di Screen Capture |
| **Status** | Info atau pesan error |

### Mode

| Mode | PPT/PPTX | PDF / Google Slides |
|---|---|---|
| **Auto** (default) | Animated kalau PowerPoint terinstal, kalau tidak Static | Static |
| **Animated (needs PowerPoint)** | Slideshow PowerPoint asli di-capture. **Animasi, transisi, dan video dalam slide ikut jalan.** Tanpa PowerPoint akan muncul pesan error | Static |
| **Static (no animation)** | Dikonversi ke PDF lewat PowerPoint atau LibreOffice, lalu ditampilkan per halaman. **PowerPoint opsional** | Static |

Cara kerja mode Animated:
- **Windows:** plugin membuka file di PowerPoint (read-only) dan menjalankan slideshow tanpa bingkai seukuran composition (misalnya 1920×1080). Jendelanya dipindah ke luar layar lalu di-capture. Setelah itu fokus dikembalikan ke Resolume. Kalau Loop mati, Next di langkah terakhir tidak membuka layar hitam "End of slide show".
- **macOS:** slideshow berjalan dalam mode jendela (*Browsed by an individual*) dan di-capture. Kalau title bar atau status bar PowerPoint ikut tampil, rapikan dengan **Crop Top/Bottom**.
- Saat file diganti atau plugin dihapus, presentasi ditutup. PowerPoint juga ditutup kalau plugin yang membukanya dan tidak ada dokumen lain yang terbuka.

Mode Static:
- Hasil konversi disimpan di folder temp, jadi file yang sama langsung terbuka di pemakaian berikutnya. Kalau file PPT diedit, cache otomatis diperbarui.
- Halaman dirender seukuran composition, dan halaman berikutnya disiapkan lebih dulu supaya Next terasa instan.

### Google Slides
1. Di Google Slides: **Share → General access → Anyone with the link (Viewer)**.
2. Salin link-nya (`https://docs.google.com/presentation/d/.../edit`) ke **Google Slides URL**.
3. Plugin mengunduhnya sebagai PDF. Google Slides tidak menyertakan animasi di ekspor PDF.
4. Link **Publish to web** (`/d/e/2PACX...`) tidak bisa dipakai. Gunakan link Share biasa.

> Butuh Google Slides dengan animasi? Jalankan presentasinya di browser, lalu capture jendela browser memakai **SC Screen Capture**.

---

## Status pengujian (penting)

Sudah diuji:
- Kode bersama dan logika kedua plugin di-compile dan dijalankan di host FFGL headless (`tests/ffgl_host_smoke.cpp`, 15 cek: parameter, dropdown dinamis, render, simpan jendela, pesan error, pembatalan konversi, unload tanpa hang). Semua lolos.
- Konversi PPTX ke PDF lewat LibreOffice, termasuk cache dan nama file berspasi.
- Build Windows (MSVC) dan macOS (universal) lolos di GitHub Actions, termasuk installer yang dites di CI: `.exe` dipasang lalu di-uninstall, `.pkg` dipasang (script uninstall Mac belum dites).

**Belum** diuji karena belum dijalankan di Resolume sungguhan:
- Perilaku nyata di Resolume dan PowerPoint, terutama:
  - Apakah jendela slideshow yang dipindah ke luar layar tetap ter-capture di Windows. Kalau hasilnya hitam, matikan **Hide PowerPoint Window**.
  - Perintah AppleScript PowerPoint for Mac (`go to next slide`, `show type`). Istilahnya bisa berbeda antar versi Office.

Checklist tes di Resolume:
1. Screen Capture → Monitor: gambar tampil, Show Cursor on/off bekerja.
2. Screen Capture → Window: pilih browser, tutup jendela lain di atasnya, gambar tetap jalan. Simpan composition, buka lagi, jendela yang sama terpilih.
3. Presentation → PPTX dengan animasi, Mode Auto: Next menjalankan animasi satu per satu.
4. Presentation → Mode Static tanpa PowerPoint (LibreOffice terinstal): slide tampil.
5. Presentation → PDF dan Google Slides.
6. Mapping Next/Prev ke MIDI atau keyboard.

---

## Struktur kode
```
src/common/   Frame, Worker, Platform API, DocumentPrep (PPT/Google ke PDF), FrameRenderer (OpenGL)
src/win/      Windows.Graphics.Capture, Windows.Data.Pdf, PowerPoint COM, proses/berkas
src/mac/      ScreenCaptureKit, CoreGraphics PDF, PowerPoint AppleScript
src/posix/    Proses/berkas untuk macOS dan Linux
src/linux/    Stub untuk uji di Linux (Linux bukan platform Resolume)
src/plugins/  ScreenCapturePlugin, PresentationPlugin
tests/        Host FFGL headless untuk smoke test
```
