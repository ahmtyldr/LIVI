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

## UiBridge — sözleşmenin çalışma zamanı karşılığı

Ana süreç, `contracts/ui-api.json`'daki her çağrıyı ve olayı bir Unix soketinde sunar
(`src/main/ui-bridge/`): `$XDG_RUNTIME_DIR/livi-ui.sock`, satır sonu ayrılmış JSON-RPC 2.0.

- Çağrı: `{"jsonrpc":"2.0","id":1,"method":"projection.settings.get","params":[]}`
- Olay (sunucudan bildirim): `{"jsonrpc":"2.0","method":"event","params":{"channel":"telemetry:update","args":[...]}}`
- İkili veri (PCM): `{"$bytes":"<base64>"}`
- Ek metotlar: `rpc.describe` (metot ve olay listesi), `rpc.ping`; yerel değerler `app.platform`, `app.compositor`.
- Abonelik metotları (`projection.ipc.onEvent` gibi) çağrı gerektirmez: her istemci her olayı alır.

Komut satırı istemcisi `tools/ui-cli.mjs`:

```bash
tools/ui-cli.mjs rpc.describe
tools/ui-cli.mjs projection.settings.get
tools/ui-cli.mjs projection.ipc.sendTouch 100 200 0
tools/ui-cli.mjs --watch telemetry:update
# Node kurulu olmayan kioskta, AppImage'ın kendi Node'uyla:
ELECTRON_RUN_AS_NODE=1 XDG_RUNTIME_DIR=/run/user/1000 ~/LIVI/LIVI.AppImage ~/ui-cli.mjs rpc.ping
```
