# contracts/ — arayüz sözleşmesi

Bu klasördeki dosyalar **üretilir**, elle düzenlenmez: `pnpm contracts:gen`.
CI (`typecheck.yml`) `pnpm contracts:check` ile güncel olduklarını doğrular.
Kaynak: `scripts/contracts-gen.mts` (TypeScript derleyici API'siyle statik okuma;
Electron ve React import edilmez).

| Dosya | Kaynağı | İçerik |
|---|---|---|
| `ui-api.json` | `src/preload/index.ts` | Arayüzün ana süreçle konuştuğu her çağrı: ad (`projection.ipc.getDevices`, `app.getVersion`), parametre ve dönüş tipleri, taşıma (`invoke` / `send` / `local`), IPC kanal adı; ana süreçten gelen olay kanalları |
| `config.schema.json` | `src/main/shared/types/Config.ts`, `DefaultConfig.ts` | Her config anahtarı: TS tipi, tür (`boolean`/`number`/`string`/`literal`/`enum`/…), varsayılan değer, açıklama; enum tabloları ve sabitler |
| `settings-schema.json` | `src/renderer/src/routes/schemas/*.ts` | Ayarlar ağacı (rotalar ve alanlar) JSON olarak; düz alan listesi ve tip sayıları. React bileşenleri `{"$component": "Camera"}`, seçenek yükleyiciler `{"$fn": "loadWifiChannels", "$call": "app.listWifiChannels"}`, dönüşüm fonksiyonları `{"$fn": "<kaynak>"}` olarak işaretlenir |
| `locale-keys.json` | `src/renderer/src/locales/en.json` | Düzleştirilmiş çeviri anahtarları ve dillerin eksik/fazla anahtar raporu |
| `locales/*.json` | `src/renderer/src/locales/` | Çeviri dosyalarının kopyası (livi-ui doğrudan bunları okur) |

İşaretler:
- `$component`: LVGL tarafında elle yazılacak özel widget (Camera, ColorCalibration, ContrastGammaCalibration, IconUploader, PowerOff, Restart, SoftwareUpdate, USBDongle).
- `$fn` / `$call`: çalışma zamanı fonksiyonu; `$call` varsa hangi `ui-api` çağrısına denk geldiği.
- `$expr`: çalışma zamanında hesaplanan koşul (ör. `hidden: window.app?.platform !== 'linux'`); LVGL tarafı aynı koşulu uygular.
- `$unresolved` / `$ref` / `$spread`: statik olarak çözülemeyen ifade, kaynak metniyle; sıfır olmalı.
