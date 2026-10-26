# Game identity and provenance

## Confirmed facts

| Fact | Value | Evidence |
|---|---|---|
| Title | **Cancan ile Toyota Trafik Oyunu** (box/menu art), installed as "Toyota Trafik Oyunu" / "Toyota Trafik Oyunu v2" | Menu splash (`maps/giris.jpg`) captured under Wine; MSI product names; `kur.exe` form caption |
| Campaign name | "Toyota Trafik Eğitimi Oyunu", part of ToyotaSA's "İnsana Saygı, Trafikte Saygı" social-awareness campaign | otomot.net, 16 Apr 2007 [1] |
| Publisher / commissioner | Toyotasa Toyota – Sabancı Pazarlama ve Satış A.Ş. (ToyotaSA) | `Lisans.rtf`; [1] |
| Developer | Martı Animasyon (Ali Murat Erkorkmaz and team) | `Lisans.rtf` ("Martı Animasyon'a hazırlatılmıştır"); [1] |
| Announced | 8 December 2006 press conference at Sabancı Center; protocol with MEB (Ministry of Education) primary-schools directorate | [1] |
| Distribution | Free CD from Toyota Plaza dealers; later intended for MEB school labs | [1], Technopat 2013 [2] |
| Target audience | Primary-school children (traffic education) | [1], `Lisans.rtf` |
| Platform | 32-bit Windows (Win95/98/ME/NT4/2000/XP per `Setup.ini` SupportOS lists) | `Kurulum*/Setup.ini` |
| Engine | Borland **Delphi 7** VCL application + **GLScene** (OpenGL scene graph) + **oxGLODE 1.5b** (Dave Gravel's ODE physics wrapper, `oxGLODE.dll`) | PE strings `D:\Delphi\Birimler\Arsiv\GLScene\Source\...`, Delphi VMTs/DFM resources, `OrionX3D.Tag`, imports |
| Media | VCL `TMediaPlayer` (MCI) for the Indeo 4.1 intro clip `Video/cancan.avi` and MPEG-1 car clips; MP3 playlist; WAV effects | DFM `mp1`/`mp2`; `winmm.dll` imports; ffprobe |
| Online services | None required. `web.url` points to `http://www.toyotasa.com.tr/trafikoyunu/`; menu "İletişim" offers web site / e-mail / "Puanını ToyotaSA'ya gönder" | `web.url`, menu DFM `PopupMenu1` |
| Genre | 3D traffic-education driving game: licence quiz → four maps (Eğitim Sahası, Kasaba, Şehir, Şehir Merkezi) with time limit and point target, traffic-rule scoring, in-game sign questions, spotting other road users' violations | Runtime capture under Wine; [1] |

## Releases present locally

`Toyota Trafik Oyunu Kurulum.rar` (SHA-256 `e6d2d4ee276124a087e88e05c34e0b6fcafc516019fe3bf78a5782d820d3c446`, 425,961,589 bytes) extracts to the
`Toyota Trafik Oyunu/` CD image; `diff -r` shows the folder is byte-identical to the archive.
The CD contains an autorun chooser `kur.exe` with two installers:

| Installer | Caption in `kur.exe` | MSI | Install dir | `TrafikOyunu.exe` |
|---|---|---|---|---|
| `Kurulum1` | "Kurulum 1 (Yavaş Bilgisayarlar İçin)" — slow PCs | `ToyotaTrafikOyunu.msi` | `ToyotaSA\Toyota Trafik Oyunu` | 2,209,280 B, 2007-05-01 |
| `Kurulum2` | "Kurulum 2 (Hızlı Bilgisayarlar İçin)" — fast PCs | `Toyota Trafik Oyunu v2.msi` | `ToyotaSA\Toyota Trafik Oyunu v2` | 2,220,544 B |

Distinguishing v2 from v1 (evidence: decoded DFMs and data diff): v2 enables `Buffer.AntiAliasing = aa2xHQ`, has
`demo=true` and pedestrian colour sets in `HG.vdd`, more `Insanlar` variants and higher-detail car texture
sets; HUD hints use ASCII-folded Turkish. Full per-file hashes are kept privately in `work/inventory/`.

## Identity cross-checks

- Not related to other "Toyota" or "trafik" driving games: the help text inside the program (captured under
  Wine) matches the published "Oyunu Nasıl Oynayacağım?" text by name ("Merhaba ben Afacan, Toyota Trafik
  Oyununu Cancan'la birlikte oynayacağız...").
- A Reddit r/lostmediatr post titled "[KAYIP] Toyota Trafik Oyunu" [3] reports the game as lost media; no
  public preservation, source release or prior technical analysis was found.

## Unresolved questions

- Exact v1/v2 release dates and whether other CD pressings exist (only one CD image is available).
- Whether `www.toyotatrafikoyunu.com` hosted a downloadable build or the online high-score list mentioned in
  the in-game help ("www.toyotasa.com.tr'ye gir... listede yerini al").
- Role of `iv5setup.exe` (Ligos Indeo 5 codec installer shipped beside the game; see FAILURE_ANALYSIS).

## Sources

1. "ToyotaSA Trafikte Saygı Oyunu", Otomot.NET, 16 April 2007 — <http://otomot.net/2007/04/16/toyotasa-trafikte-saygi-oyunu/>
2. "Toyota Trafik Oyunu CD'si Hakkında", Technopat Sosyal, 26 January 2013 — <https://www.technopat.net/sosyal/konu/toyota-trafik-oyunu-cdsi-hakkinda.11321/>
3. r/lostmediatr, "[KAYIP] Toyota Trafik Oyunu" — <https://www.reddit.com/r/lostmediatr/comments/1uwz2m6/kayip_toyota_trafik_oyunu>
4. Software directory entries naming ToyotaSA as publisher: <https://toyota-trafik-oyunu-v2.software.informer.com/>
