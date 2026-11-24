// Cloud-folder sync core (docs/sync.md): Argon2id, the encrypted vault and
// device files, last-writer-wins merging and cloud folder detection.
#include "sync/sync_crypto.h"
#include "sync/sync_folder.h"
#include "sync/sync_model.h"
#include "sync/sync_tree.h"
#include "sync/sync_pass.h"
#include "sync/sync_passwords.h"
#include "sync/sync_platform.h"
#include <sqlite3.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <monocypher.h>
#include <string>
#include <vector>
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <zstd.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {
namespace fs = std::filesystem;
using namespace red::sync;
int failures = 0;
void Check(bool test, const char* name) { if (!test) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } }

// Small parameters keep the suite fast; production uses KdfParams{}.
const KdfParams kFast{19456, 2, 1};
const std::string kDeviceA(32, 'a'), kDeviceB(32, 'b'), kDeviceC(32, 'c');

fs::path TempDir(const wchar_t* name) {
  wchar_t base[MAX_PATH]{};
  GetTempPathW(MAX_PATH, base);
  auto path = fs::path(base) / (std::wstring(L"sol-sync-test-") + name + L"-" + std::to_wstring(GetCurrentProcessId()));
  std::error_code ignored;
  fs::remove_all(path, ignored);
  fs::create_directories(path, ignored);
  return path;
}

std::string FromHex(std::string_view hex) {
  std::string out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<char>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
  return out;
}
std::string Argon2idRaw(std::string_view pass, std::string_view salt, uint32_t kib, uint32_t passes, uint32_t lanes,
                        std::string_view key = {}, std::string_view ad = {}) {
  std::vector<uint8_t> work(static_cast<size_t>(kib) * 1024);
  std::string out(32, '\0');
  const crypto_argon2_config config{CRYPTO_ARGON2_ID, kib, passes, lanes};
  const crypto_argon2_inputs inputs{reinterpret_cast<const uint8_t*>(pass.data()), reinterpret_cast<const uint8_t*>(salt.data()),
                                    static_cast<uint32_t>(pass.size()), static_cast<uint32_t>(salt.size())};
  const crypto_argon2_extras extras{key.empty() ? nullptr : reinterpret_cast<const uint8_t*>(key.data()), ad.empty() ? nullptr : reinterpret_cast<const uint8_t*>(ad.data()),
                                    static_cast<uint32_t>(key.size()), static_cast<uint32_t>(ad.size())};
  crypto_argon2(reinterpret_cast<uint8_t*>(out.data()), 32, work.data(), config, inputs, extras);
  return Hex(out);
}

void TestArgon2() {
  // Known answers from the Argon2 reference implementation (argon2id v19).
  Check(Argon2idRaw("password", "somesalt", 65536, 2, 1) == "09316115d5cf24ed5a15a31a3ba326e5cf32edc24702987c02b6566f61913cf7",
        "argon2id reference vector");
  // RFC 9106 section 5.3: secret key and associated data, 32 KiB, t=3, p=4.
  Check(Argon2idRaw(std::string(32, '\x01'), std::string(16, '\x02'), 32, 3, 4, std::string(8, '\x03'), std::string(12, '\x04')) ==
        "0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659", "argon2id RFC 9106 vector");
  // The wrapper with the reference parameters gives the reference answer too (16-byte salt).
  std::string wrapped;
  Check(Argon2id("password", "somesaltsomesalt", KdfParams{65536, 2, 1}, 32, wrapped) &&
        Hex(wrapped) == Argon2idRaw("password", "somesaltsomesalt", 65536, 2, 1), "wrapper matches raw Argon2id");

  std::string a, b;
  const std::string salt16(16, 's');
  Check(Argon2id("correct horse", salt16, kFast, 32, a) && a.size() == 32, "wrapper derives");
  Check(Argon2id("correct horse", salt16, kFast, 32, b) && a == b, "wrapper deterministic");
  Check(Argon2id("correct horsf", salt16, kFast, 32, b) && a != b, "wrapper passphrase sensitive");
  Check(!Argon2id("x", "short", kFast, 32, b) && b.empty(), "wrapper rejects short salt");
  Check(!Argon2id("x", salt16, KdfParams{1024, 1, 1}, 32, b), "wrapper rejects weak parameters");
  Check(!Argon2id("x", salt16, KdfParams{4u * 1024 * 1024, 3, 4}, 32, b), "wrapper rejects memory bomb");
}

void TestAead() {
  std::string key, sealed, sealed2, plain;
  Check(RandomBytes(32, key) && key.size() == 32, "random key");
  // draft-irtf-cfrg-xchacha-03, appendix A.3.1 (AEAD_XChaCha20_Poly1305).
  {
    const auto plain_text = FromHex("4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a204966204920636f756c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73637265656e20776f756c642062652069742e");
    const auto aad = FromHex("50515253c0c1c2c3c4c5c6c7");
    const auto vector_key = FromHex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    const auto nonce = FromHex("404142434445464748494a4b4c4d4e4f5051525354555657");
    const auto cipher = FromHex("bd6d179d3e83d43b9576579493c0e939572a1700252bfaccbed2902c21396cbb731c7f1b0b4aa6440bf3a82f4eda7e39ae64c6708c54c216cb96b72e1213b4522f8c9ba40db5d945b11b69b982c1bb9e3f3fac2bc369488f76b2383565d3fff921f9664c97637da9768812f615c68b13b52e");
    const auto tag = FromHex("c0875924c1c7987947deafd8780acf49");
    std::string opened;
    Check(Open(vector_key, nonce + cipher + tag, aad, opened) && opened == plain_text, "xchacha20-poly1305 draft vector");
  }
  Check(Seal(key, "secret value", "aad", sealed) && sealed.size() == 24 + 12 + 16, "seal");
  Check(Seal(key, "secret value", "aad", sealed2) && sealed != sealed2, "fresh nonce each seal");
  Check(Open(key, sealed, "aad", plain) && plain == "secret value", "open");
  Check(!Open(key, sealed, "other", plain) && plain.empty(), "aad bound");
  auto tampered = sealed; tampered[26] ^= 1;
  Check(!Open(key, tampered, "aad", plain), "tamper detected");
  std::string other; RandomBytes(32, other);
  Check(!Open(other, sealed, "aad", plain), "wrong key rejected");
  Check(Seal(key, "", "aad", sealed) && Open(key, sealed, "aad", plain) && plain.empty(), "empty plaintext");
  std::string blob, back;
  Check(platform::ProtectLocalSecret(key, blob) && blob != key && platform::UnprotectLocalSecret(blob, back) && back == key, "local secret round trip");
  auto broken_blob = blob; broken_blob[broken_blob.size() / 2] ^= 1;
  Check(!platform::UnprotectLocalSecret(broken_blob, back) && back.empty(), "damaged local secret rejected");

  // RFC 4648 section 10.
  const char* rfc_plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char* rfc_encoded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  bool vectors = true;
  std::string decoded;
  for (int i = 0; i < 7; ++i) {
    vectors = vectors && Base64Encode(rfc_plain[i]) == rfc_encoded[i];
    if (i) vectors = vectors && Base64Decode(rfc_encoded[i], decoded) && decoded == rfc_plain[i];
  }
  Check(vectors, "base64 RFC 4648 vectors");
  Check(Base64Decode(Base64Encode(std::string_view("\x00\xff hi", 5)), decoded) && decoded == std::string("\x00\xff hi", 5), "base64 binary round trip");
  Check(!Base64Decode("ab\ncd", decoded) && !Base64Decode("a b ", decoded), "base64 rejects whitespace");
  Check(!Base64Decode("Zg=", decoded) && !Base64Decode("Z=g=", decoded) && !Base64Decode("Zh==", decoded) && !Base64Decode("Zm8=Zm8=", decoded),
        "base64 rejects non-canonical input");
}

void TestCompression() {
  std::string json = "{\"records\":[";
  for (int i = 0; i < 2000; ++i) json += "{\"k\":\"node/00000000-0000-4000-8000-" + std::to_string(100000000000 + i) + "\",\"t\":1700000000000,\"c\":0,\"d\":\"" + kDeviceA + "\",\"v\":\"{\\\"title\\\":\\\"Page " + std::to_string(i) + "\\\"}\"},";
  json += "{}]}";
  std::string frame, back;
  Check(Compress(json, frame) && frame.size() * 4 < json.size(), "zstd compresses sync JSON more than 4x");
  Check(Decompress(frame, back) && back == json, "zstd round trip");
  Check(Compress("", frame) && Decompress(frame, back) && back.empty(), "empty payload");
  Check(!Decompress("not a zstd frame", back) && back.empty(), "garbage refused");
  // A frame header that claims more than the limit is refused before allocating.
  std::string big(kMaxPlainSize + 1, 'a');
  Check(Compress(std::string_view(big).substr(0, kMaxPlainSize), frame) && Decompress(frame, back) && back.size() == kMaxPlainSize, "payload at the limit accepted");
  Check(!Compress(big, frame), "payload over the limit not compressed");
  std::string streamed(ZSTD_compressBound(big.size()), '\0');
  ZSTD_CCtx* context = ZSTD_createCCtx();
  ZSTD_inBuffer in{big.data(), big.size(), 0};
  ZSTD_outBuffer out{streamed.data(), streamed.size(), 0};
  ZSTD_CCtx_setPledgedSrcSize(context, big.size());
  const size_t rest = ZSTD_compressStream2(context, &out, &in, ZSTD_e_end);
  ZSTD_freeCCtx(context);
  streamed.resize(out.pos);
  Check(rest == 0 && !Decompress(streamed, back) && back.empty(), "decompression bomb refused");
  std::string unknown(ZSTD_compressBound(10), '\0');
  context = ZSTD_createCCtx();
  ZSTD_CCtx_setParameter(context, ZSTD_c_contentSizeFlag, 0);
  const size_t unknown_size = ZSTD_compress2(context, unknown.data(), unknown.size(), "0123456789", 10);
  ZSTD_freeCCtx(context);
  unknown.resize(ZSTD_isError(unknown_size) ? 0 : unknown_size);
  Check(!unknown.empty() && !Decompress(unknown, back), "frame without content size refused");
}

void TestMerge() {
  Clock a(kDeviceA), b(kDeviceB);
  State base_a, base_b;
  Values local_a{{"space/1", "{\"name\":\"Work\"}"}, {"password/default/https://x.test/\nann", "{\"password\":\"p1\"}"}};
  Check(StampLocalChanges(base_a, local_a, a, 1000) == 2, "initial local records");
  Check(StampLocalChanges(base_a, local_a, a, 1001) == 0, "unchanged values keep stamps");

  // B starts empty, merges A.
  Values local_b;
  StampLocalChanges(base_b, local_b, b, 500);
  Check(Merge(base_b, base_a) == 2, "merge takes new records");
  auto changes = Diff(base_b, local_b);
  Check(changes.upsert.size() == 2 && changes.remove.empty(), "diff applies remote records");
  local_b = Live(base_b);

  // B's clock is behind A's, but an edit after seeing A's record still wins.
  local_b["space/1"] = "{\"name\":\"Work B\"}";
  StampLocalChanges(base_b, local_b, b, 900);
  Check(base_b["space/1"].stamp > base_a["space/1"].stamp, "observed stamps advance a slow clock");

  // Concurrent: A deletes the password, B renames the space.
  local_a.erase("password/default/https://x.test/\nann");
  StampLocalChanges(base_a, local_a, a, 2000);
  Check(base_a["password/default/https://x.test/\nann"].deleted, "local delete becomes tombstone");

  State merged_a = base_a, merged_b = base_b;
  Merge(merged_a, base_b);
  Merge(merged_b, base_a);
  Check(merged_a == merged_b, "merge is commutative");
  Check(merged_a["space/1"].value == "{\"name\":\"Work B\"}", "later rename wins");
  Check(merged_a["password/default/https://x.test/\nann"].deleted, "later delete wins");
  State twice = merged_a; Merge(twice, merged_a);
  Check(twice == merged_a, "merge is idempotent");

  changes = Diff(merged_b, local_b);
  Check(changes.remove.size() == 1 && changes.upsert.empty(), "diff removes tombstoned key");

  // A re-adds after deleting: the re-add has a newer stamp and wins.
  local_a["password/default/https://x.test/\nann"] = "{\"password\":\"p2\"}";
  base_a = merged_a;
  StampLocalChanges(base_a, local_a, a, 2001);
  Merge(merged_b, base_a);
  Check(!merged_b["password/default/https://x.test/\nann"].deleted && merged_b["password/default/https://x.test/\nann"].value == "{\"password\":\"p2\"}",
        "re-add after delete wins");

  // Same-millisecond tie between devices is broken by device id on every PC.
  State x{{"space/9", Record{Stamp{5000, 0, kDeviceA}, false, "a"}}}, y{{"space/9", Record{Stamp{5000, 0, kDeviceB}, false, "b"}}};
  State xy = x, yx = y; Merge(xy, y); Merge(yx, x);
  Check(xy == yx && xy["space/9"].value == "b", "device id breaks ties");
}

void TestSerialize() {
  State state{{"space/1", Record{Stamp{1700000000000, 3, kDeviceA}, false, "{\"name\":\"Q\\\"\\n\xd0\x96\"}"}},
              {"node/2", Record{Stamp{1700000000001, 0, kDeviceB}, true, ""}},
              {"password/p/https://a.test/\nu\x01", Record{Stamp{1700000000002, 0, kDeviceC}, false, std::string("v\0x", 3)}}};
  State back;
  Check(Parse(Serialize(state), back) && back == state, "serialize round trip");
  Check(!Parse("{\"records\":[{\"k\":\"space/1\",\"t\":1,\"c\":0,\"d\":\"zz\",\"v\":\"x\"}]}", back) && back.empty(), "rejects bad device");
  Check(!Parse("{\"records\":[{\"k\":\"nokind\",\"t\":1,\"c\":0,\"d\":\"" + kDeviceA + "\",\"v\":\"x\"}]}", back), "rejects bad key");
  Check(!Parse("{\"records\":[{\"k\":\"a/b\",\"t\":1,\"c\":0,\"d\":\"" + kDeviceA + "\",\"v\":\"x\",\"x\":true}]}", back), "rejects value with tombstone");
  Check(!Parse("{\"records\":[{\"k\":\"a/b\",\"t\":1,\"c\":0,\"d\":\"" + kDeviceA + "\"}]}", back), "rejects record without value");
  Check(!Parse("{\"records\":[{\"k\":\"a/b\",\"t\":1,\"c\":0,\"d\":\"" + kDeviceA + "\",\"x\":true},{\"k\":\"a/b\",\"t\":2,\"c\":0,\"d\":\"" + kDeviceA + "\",\"x\":true}]}", back),
        "rejects duplicate keys");
  Check(!Parse("{\"records\":[{\"k\":\"a/b\",\"t\":99999999999999999,\"c\":0,\"d\":\"" + kDeviceA + "\",\"x\":true}]}", back), "rejects far-future stamp");
  Check(!Parse("not json", back) && !Parse("{}", back), "rejects malformed");
}

void TestVault() {
  const auto root = TempDir(L"vault");
  const auto dir = SyncDirectory(root);
  OpenedVault vault, again, other;
  Check(OpenVault(dir, "whatever passphrase", vault) == VaultStatus::Missing, "missing vault");
  Check(CreateVault(dir, "short", vault, kFast) == VaultStatus::WeakPassphrase, "weak passphrase refused");
  Check(CreateVault(dir, "correct horse battery", vault, kFast) == VaultStatus::Ok && vault.key.size() == 32 && vault.id.size() == 16, "create vault");
  Check(CreateVault(dir, "another passphrase!", other, kFast) == VaultStatus::Exists && other.key.empty(), "never replaces a vault");
  Check(OpenVault(dir, "correct horse battery", again) == VaultStatus::Ok && again.key == vault.key && again.id == vault.id, "open vault");
  Check(OpenVault(dir, "correct horse batterY", other) == VaultStatus::WrongPassphrase && other.key.empty(), "wrong passphrase");
  Check(PeekVaultId(dir) == vault.id, "peek vault id");

  std::string text;
  ReadFileLimited(dir / L"vault.json", 1 << 16, text);
  Check(text.find("correct") == std::string::npos && text.find("argon2id") != std::string::npos, "vault.json holds no passphrase");

  Check(ChangePassphrase(dir, vault, "a new long passphrase", kFast) == VaultStatus::Ok, "change passphrase");
  Check(OpenVault(dir, "correct horse battery", other) == VaultStatus::WrongPassphrase, "old passphrase stops working");
  Check(OpenVault(dir, "a new long passphrase", other) == VaultStatus::Ok && other.key == vault.key, "same key after change");

  // Untrusted vault.json: absurd KDF parameters are refused before deriving.
  const auto bomb_dir = SyncDirectory(TempDir(L"bomb"));
  fs::create_directories(bomb_dir);
  AtomicWrite(bomb_dir / L"vault.json", "{\"format\":1,\"vault\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"kdf\":{\"alg\":\"argon2id\",\"m\":99999999,\"t\":3,\"p\":4},\"cipher\":\"xchacha20-poly1305\","
                                         "\"salt\":\"AAAAAAAAAAAAAAAAAAAAAA==\",\"key\":\"" + std::string(96, 'A') + "\"}");
  Check(OpenVault(bomb_dir, "correct horse battery", other) == VaultStatus::Corrupt, "memory bomb vault refused");
  AtomicWrite(bomb_dir / L"vault.json", "{\"format\":2}");
  Check(OpenVault(bomb_dir, "correct horse battery", other) == VaultStatus::Corrupt, "unknown format refused");

  // Device files.
  State state{{"password/default/https://bank.test/\nme", Record{Stamp{1700000000000, 0, kDeviceA}, false, "{\"password\":\"hunter2-secret\"}"}}};
  Check(WriteDeviceFile(dir, vault, kDeviceA, state), "write device file");
  Check(WriteDeviceFile(dir, vault, kDeviceB, State{}), "write second device file");
  std::string raw;
  ReadFileLimited(dir / L"devices" / (std::wstring(kDeviceA.begin(), kDeviceA.end()) + L".sol"), 1 << 20, raw);
  Check(!raw.empty() && raw.find("hunter2") == std::string::npos && raw.find("bank.test") == std::string::npos, "device file is encrypted");
  Check(!fs::exists(dir / L"devices" / (std::wstring(kDeviceA.begin(), kDeviceA.end()) + L".sol.tmp")), "no temporary file left");

  // A copy under another name, a conflict copy and a tampered file.
  fs::copy_file(dir / L"devices" / (std::wstring(kDeviceA.begin(), kDeviceA.end()) + L".sol"),
                dir / L"devices" / (std::wstring(kDeviceC.begin(), kDeviceC.end()) + L".sol"));
  fs::copy_file(dir / L"devices" / (std::wstring(kDeviceA.begin(), kDeviceA.end()) + L".sol"),
                dir / L"devices" / (std::wstring(kDeviceA.begin(), kDeviceA.end()) + L" (2).sol"));
  auto files = ReadDeviceFiles(dir, vault);
  Check(files.size() == 3, "conflict copy skipped");
  Check(files.size() == 3 && files[0].device == kDeviceA && files[0].ok && files[0].state == state, "device file read back");
  Check(files.size() == 3 && files[1].device == kDeviceB && files[1].ok && files[1].state.empty(), "empty device file");
  Check(files.size() == 3 && files[2].device == kDeviceC && !files[2].ok && files[2].state.empty(), "renamed copy fails authentication");

  // Another vault cannot read these files.
  const auto foreign_dir = SyncDirectory(TempDir(L"foreign"));
  OpenedVault foreign;
  CreateVault(foreign_dir, "correct horse battery", foreign, kFast);
  std::string envelope; State out;
  SealState(vault, kDeviceA, state, envelope);
  Check(!OpenState(foreign, kDeviceA, envelope, out) && out.empty(), "other vault rejected");
  Check(OpenState(vault, kDeviceA, envelope, out) && out == state, "own vault accepted");

  Check(RemoveDeviceFile(dir, kDeviceB) && ReadDeviceFiles(dir, vault).size() == 2, "remove device file");
  Check(!RemoveDeviceFile(dir, "..\\vault"), "remove rejects paths");
  Check(ValidDeviceId(NewDeviceId()), "new device id");
  std::error_code ignored;
  for (const auto& path : {root, bomb_dir.parent_path(), foreign_dir.parent_path()}) fs::remove_all(path, ignored);
}

void TestDetection() {
  const auto root = TempDir(L"detect");
  for (const wchar_t* name : {L"OneDrive", L"OneDrive - Contoso", L"Dropbox", L"Dropbox (Work)", L"G\\My Drive"}) fs::create_directories(root / name);
  DetectionInputs inputs;
  inputs.onedrive = {root / L"OneDrive", root / L"OneDrive - Contoso", root / L"OneDrive", root / L"Missing", fs::path(L"relative")};
  const auto dropbox = (root / L"Dropbox").u8string(), work = (root / L"Dropbox (Work)").u8string();
  auto escape = [](std::u8string text) { std::string out; for (const char8_t c : text) { if (c == u8'\\') out += "\\\\"; else out.push_back(static_cast<char>(c)); } return out; };
  inputs.dropbox_info = {"{\"personal\":{\"path\":\"" + escape(dropbox) + "\"},\"business\":{\"path\":\"" + escape(work) + "\"}}", "garbage", "[]"};
  inputs.google_drive = {root / L"G" / L"My Drive", root / L"NoDrive" / L"My Drive"};
  const auto folders = DetectCloudFolders(inputs);
  Check(folders.size() == 5, "detects existing folders once");
  Check(folders.size() == 5 && folders[0].provider == Provider::OneDrive && folders[1].provider == Provider::OneDrive &&
        folders[2].provider == Provider::Dropbox && folders[3].provider == Provider::Dropbox && folders[4].provider == Provider::GoogleDrive,
        "provider order");
  Check(folders.size() == 5 && folders[4].path.filename() == L"My Drive", "google drive folder");
  // Streaming volumes: My Drive in the account's language, beside hidden folders and Shared drives.
  const auto volume = [&](const wchar_t* name, std::initializer_list<const wchar_t*> folders) {
    for (const auto* folder : folders) fs::create_directories(root / name / folder);
    std::ofstream(root / name / L"loose.txt") << "x";
    DetectionInputs only; only.google_drive_volumes = {root / name};
    return DetectCloudFolders(only);
  };
  const std::wstring my_drive_uk = L"Мій диск", shared_uk = L"Спільні диски";
  const auto localized = volume(L"H", {my_drive_uk.c_str(), L".Encrypted", L".shortcut-targets-by-id", L"$RECYCLE.BIN"});
  Check(localized.size() == 1 && localized[0].provider == Provider::GoogleDrive && localized[0].path.filename() == my_drive_uk,
        "localized My Drive on a Google Drive volume");
  const auto workspace = volume(L"W", {my_drive_uk.c_str(), shared_uk.c_str(), L"$RECYCLE.BIN"});
  Check(workspace.size() == 1 && workspace[0].path.filename() == my_drive_uk, "Shared drives is not My Drive");
  const auto english = volume(L"E", {L"My Drive", L"Shared drives", L"Other"});
  Check(english.size() == 1 && english[0].path.filename() == L"My Drive", "My Drive wins on an English volume");
  // The laptop in the two-PC test: Russian My Drive beside Russian "Other computers".
  const std::wstring my_drive_ru_name = L"Мой диск";
  const std::wstring other_ru = L"Другие компьютеры";
  const auto laptop = volume(L"R", {my_drive_ru_name.c_str(), other_ru.c_str(), L".Encrypted", L".shortcut-targets-by-id", L"$RECYCLE.BIN"});
  Check(laptop.size() == 1 && laptop[0].path.filename() == my_drive_ru_name, "Other computers is not My Drive");
  const auto german = volume(L"D", {L"Meine Ablage", L"Andere Computer"});
  Check(german.size() == 1 && german[0].path.filename() == L"Meine Ablage", "a known My Drive name wins over unknown folders");
  Check(volume(L"A", {L"One", L"Two"}).empty() && volume(L"Z", {}).empty(), "ambiguous or empty volumes are not guessed");
  // Drive for desktop's SyncTargets (protobuf): field 1 per account holding the mount as field 2.
  const auto field = [](int number, const std::string& value) {
    return std::string(1, static_cast<char>(number << 3 | 2)) + static_cast<char>(value.size()) + value;
  };
  const std::string account_g = field(1, field(1, "123456789")) + field(2, "G:") + field(3, "E:\\backup");
  const std::string streamed_root = (root / L"Streamed").string();
  const std::string account_folder = field(1, field(1, "987")) + field(2, streamed_root);
  const std::string blob = field(1, account_g) + field(1, account_folder);
  const auto mounts = GoogleDriveMountPoints(blob);
  Check(mounts.size() == 2 && mounts[0] == "G:" && mounts[1] == streamed_root, "Drive for desktop mount points, backups ignored");
  Check(GoogleDriveMountPoints(blob.substr(0, blob.size() - 3)).size() == 1 && GoogleDriveMountPoints("").empty() &&
        GoogleDriveMountPoints(std::string("\x0a\xff\xff\xff\xff\x0f", 6)).empty() && GoogleDriveMountPoints("\x08\x01").empty(),
        "malformed SyncTargets stop parsing without overreading");
  const std::wstring my_drive_ru = L"\u041c\u043e\u0439 \u0434\u0438\u0441\u043a";
  fs::create_directories(root / L"Streamed" / my_drive_ru); fs::create_directories(root / L"Streamed" / L".Encrypted");
  DetectionInputs streamed; streamed.google_drive_sync_targets = {field(1, account_folder)};
  const auto streamed_folders = DetectCloudFolders(streamed);
  Check(streamed_folders.size() == 1 && streamed_folders[0].provider == Provider::GoogleDrive && streamed_folders[0].path.filename() == my_drive_ru,
        "Google Drive found from its registry record, whatever the volume label");
  // Drive for desktop quit: the streamed drive is gone, but Google Drive stays listed as unavailable.
  DetectionInputs unmounted;
  unmounted.google_drive_sync_targets = {field(1, field(1, "1") + field(2, (root / L"Unmounted").string()))};
  const auto away = DetectCloudFolders(unmounted);
  Check(away.size() == 1 && !away[0].available && away[0].provider == Provider::GoogleDrive && away[0].path == root / L"Unmounted",
        "an unmounted Google Drive is listed as unavailable");
  Check(ProviderForFolder(away, root / L"Unmounted" / my_drive_ru) == Provider::GoogleDrive && !ProviderForFolder(away, root / L"Unmountedx").has_value(),
        "a folder on an unmounted Google Drive is still Google Drive");
  Check(streamed_folders.size() == 1 && streamed_folders[0].available, "a mounted Google Drive is available");
  // The provider of a chosen folder, also for a subfolder; the running cloud app.
  fs::create_directories(root / L"OneDrive" / L"Docs" / L"Sol");
  Check(ProviderForFolder(folders, root / L"OneDrive" / L"Docs" / L"Sol") == Provider::OneDrive, "a subfolder of OneDrive is OneDrive");
  Check(ProviderForFolder(folders, root / L"Dropbox (Work)") == Provider::Dropbox && !ProviderForFolder(folders, root).has_value() &&
        !ProviderForFolder(folders, {}).has_value(), "only detected folders have a provider");
  const std::vector<std::wstring> running = {L"explorer.exe", L"onedrive.exe", L"googledrivefs.exe"};
  Check(ClientRunning(Provider::OneDrive, running) && ClientRunning(Provider::GoogleDrive, running) && !ClientRunning(Provider::Dropbox, running),
        "cloud app running by process name");
  Check(platform::RunningProcessNames().has_value() &&
        std::ranges::count(*platform::RunningProcessNames(), std::wstring(L"sync_test.exe")) >= 1, "this test sees itself running");
  GatherDetectionInputs();  // Must not crash on this PC; results are machine-specific.
  std::error_code ignored;
  fs::remove_all(root, ignored);
}
std::string Id(int n) {
  char text[37];
  std::snprintf(text, sizeof(text), "00000000-0000-4000-8000-%012d", n);
  return text;
}
red::TreeNode Node(int id, int position, const char* title, const char* url = "https://example.test/") {
  red::TreeNode n; n.id = Id(id); n.position = position; n.title = title; n.url = url; return n;
}
const red::TreeNode* FindIn(const red::TreeSnapshot& tree, int id) {
  for (const auto& n : tree.nodes) if (n.id == Id(id)) return &n;
  return nullptr;
}
// Space 1 (active) and Space 2; a Favorite, a Pinned folder with a child, a
// Pinned page, a Today tab and an archived tab in Space 1; a Pinned page in Space 2.
red::TreeSnapshot SampleTree() {
  red::TreeSnapshot t;
  red::Space s1; s1.id = Id(1); s1.name = "Work"; s1.icon = "W"; s1.position = 0;
  red::Space s2; s2.id = Id(2); s2.name = "Home"; s2.position = 1; s2.theme_hue = 120;
  t.spaces = {s1, s2}; t.active_space = s1.id;
  auto fav = Node(10, 0, "Mail"); fav.pinned = true; fav.favorite = true;
  auto folder = Node(11, 1, "Docs", ""); folder.folder = true; folder.pinned = true; folder.space = s1.id; folder.expanded = false;
  auto child = Node(12, 0, "Spec"); child.pinned = true; child.parent = folder.id;
  auto pinned = Node(13, 2, "Tracker"); pinned.pinned = true; pinned.space = s1.id; pinned.last_active_at = 5;
  auto today = Node(14, 3, "Search"); today.space = s1.id;
  auto archived = Node(15, 4, "Old"); archived.archived = true; archived.space = s1.id;
  auto home = Node(16, 5, "Recipes"); home.pinned = true; home.space = s2.id;
  t.nodes = {fav, folder, child, pinned, today, archived, home};
  t.active_id = today.id;
  return t;
}

void TestTree() {
  const auto tree = SampleTree();
  Check(red::TreeStore::ValidateSnapshot(tree), "sample tree valid");
  const auto values = TreeValues(tree);
  Check(values.contains("profile/default") && values.contains("space/" + Id(1)) && values.contains("space/" + Id(2)), "profiles and spaces synced");
  Check(values.contains("node/" + Id(10)) && values.contains("node/" + Id(11)) && values.contains("node/" + Id(12)) &&
        values.contains("node/" + Id(13)) && values.contains("node/" + Id(16)), "pinned, favorites and folders synced");
  Check(!values.contains("node/" + Id(14)) && !values.contains("node/" + Id(15)), "today and archived stay local");
  Check(values.size() == 8, "nothing else synced");
  Check(std::all_of(values.begin(), values.end(), [](const auto& item) { return IsTreeKey(item.first); }), "tree keys");

  auto same = ApplyTreeValues(tree, values);
  Check(same.ok && !same.changed, "applying own values changes nothing");

  // Another PC renamed a Space, moved the child out of the folder, collapsed
  // nothing (expansion is local) and added a Pinned page.
  auto remote = tree;
  remote.spaces[0].name = "Work (renamed)";
  remote.nodes[2].parent.clear(); remote.nodes[2].space = Id(1); remote.nodes[2].position = 7;
  remote.nodes[1].expanded = true;
  auto added = Node(17, 8, "New pinned"); added.pinned = true; added.space = Id(1);
  remote.nodes.push_back(added);
  remote.nodes.erase(remote.nodes.begin() + 4, remote.nodes.begin() + 6);  // Remote PC has its own Today/archive.
  for (auto& n : remote.nodes) if (n.parent.empty()) n.position = n.id == Id(17) ? 6 : n.position;
  auto applied = ApplyTreeValues(tree, TreeValues(remote));
  Check(applied.ok && applied.changed, "remote changes applied");
  Check(applied.tree.spaces[0].name == "Work (renamed)", "space renamed");
  Check(FindIn(applied.tree, 12) && FindIn(applied.tree, 12)->parent.empty() && FindIn(applied.tree, 12)->space == Id(1), "child moved to root");
  Check(FindIn(applied.tree, 17) && FindIn(applied.tree, 17)->pinned, "new pinned page added");
  Check(FindIn(applied.tree, 11) && !FindIn(applied.tree, 11)->expanded, "folder expansion stays local");
  Check(FindIn(applied.tree, 13) && FindIn(applied.tree, 13)->last_active_at == 5, "local activity kept");
  Check(FindIn(applied.tree, 14) && FindIn(applied.tree, 15) && applied.tree.active_id == Id(14), "local today, archive and active page kept");
  Check(TreeValues(applied.tree) == TreeValues(remote), "applied tree reproduces the synced values");

  // A folder deleted on one PC while another added a page into it: the page
  // moves to the end of its Space's Pinned.
  auto values2 = values;
  values2.erase("node/" + Id(11));
  auto orphan = ApplyTreeValues(tree, values2);
  Check(orphan.ok && FindIn(orphan.tree, 12) && FindIn(orphan.tree, 12)->parent.empty() && FindIn(orphan.tree, 12)->space == Id(1) &&
        FindIn(orphan.tree, 12)->position > FindIn(orphan.tree, 13)->position, "orphan moves to the end of Pinned");

  // A Space deleted elsewhere takes this PC's Today and archive in it along.
  auto values3 = values;
  values3.erase("space/" + Id(1));
  auto gone = ApplyTreeValues(tree, values3);
  Check(gone.ok && gone.tree.spaces.size() == 1 && gone.tree.active_space == Id(2) && gone.tree.spaces[0].position == 0, "space removed");
  Check(gone.ok && !FindIn(gone.tree, 11) && !FindIn(gone.tree, 12) && !FindIn(gone.tree, 13) && !FindIn(gone.tree, 14) &&
        !FindIn(gone.tree, 15) && FindIn(gone.tree, 10) && gone.tree.active_id.empty(), "space contents removed, favorites kept");

  // Two PCs moved folders into each other.
  auto cyc = tree;
  auto folder2 = Node(18, 6, "Other", ""); folder2.folder = true; folder2.pinned = true; folder2.space = Id(1);
  cyc.nodes.push_back(folder2);
  auto cyc_values = TreeValues(cyc);
  auto into = [&](int id, int parent) {
    auto& v = cyc_values["node/" + Id(id)];
    const auto start = v.find("\"parent\":\"") + 10;
    v.replace(start, v.find('"', start) - start, Id(parent));
  };
  into(11, 18); into(18, 11);
  auto cycle = ApplyTreeValues(cyc, cyc_values);
  Check(cycle.ok && (FindIn(cycle.tree, 11)->parent.empty() || FindIn(cycle.tree, 18)->parent.empty()), "folder cycle broken");

  // Favorites beyond the limit become Pinned in a Space of their profile.
  auto many = tree;
  for (int i = 0; i < 13; ++i) {
    auto fav = Node(100 + i, 6 + i, "Fav"); fav.pinned = true; fav.favorite = true; many.nodes.push_back(fav);
  }
  auto many_values = TreeValues(many);
  auto limited = ApplyTreeValues(tree, many_values);
  int favorites = 0;
  for (const auto& n : limited.tree.nodes) favorites += n.favorite;
  Check(limited.ok && favorites == 12, "favorites limited to 12");
  Check(limited.ok && FindIn(limited.tree, 112) && !FindIn(limited.tree, 112)->favorite && FindIn(limited.tree, 112)->space == Id(1), "extra favorite pinned");

  // Damaged records are ignored, never applied.
  auto bad = values;
  bad["node/" + Id(13)] = "{\"parent\":\"\",\"section\":\"pinned\",\"space\":\"" + Id(1) + "\",\"profile\":\"\",\"folder\":false,\"title\":\"x\",\"custom_title\":false,\"url\":\"javascript:alert(1)\",\"order\":0}";
  bad["node/not-an-id"] = values.at("node/" + Id(16));
  bad["space/" + Id(3)] = "{\"name\":\"\\u0001\"}";
  auto filtered = ApplyTreeValues(tree, bad);
  Check(filtered.ok && !FindIn(filtered.tree, 13) && filtered.tree.spaces.size() == 2, "invalid records ignored");
  Check(!ApplyTreeValues(tree, Values{}).ok, "no spaces: nothing applied");

  // A folder's glyph travels with it. A plain folder's record has no "icon" key, exactly as older builds wrote it.
  auto iconic = tree;
  for (auto& n : iconic.nodes) if (n.id == Id(11)) n.icon = "rocket";
  auto iconic_values = TreeValues(iconic);
  Check(iconic_values.at("node/" + Id(11)).find("\"icon\":\"rocket\"") != std::string::npos &&
        values.at("node/" + Id(11)).find("\"icon\"") == std::string::npos, "only a folder with a glyph writes one");
  auto received = ApplyTreeValues(tree, iconic_values);
  Check(received.ok && FindIn(received.tree, 11) && FindIn(received.tree, 11)->icon == "rocket", "a synced glyph arrives");
  auto cleared = ApplyTreeValues(iconic, values);
  Check(cleared.ok && FindIn(cleared.tree, 11) && FindIn(cleared.tree, 11)->icon.empty(), "a glyph removed elsewhere is removed here");
  auto tab_icon = values;
  auto& tracker = tab_icon["node/" + Id(13)];
  tracker.insert(tracker.size() - 1, ",\"icon\":\"star\"");
  auto bad_name = iconic_values;
  auto& docs = bad_name["node/" + Id(11)];
  docs.replace(docs.find("rocket"), 6, "Rocket");
  Check(ApplyTreeValues(tree, tab_icon).ok && !FindIn(ApplyTreeValues(tree, tab_icon).tree, 13), "a page record with a glyph is ignored");
  Check(ApplyTreeValues(tree, bad_name).ok && !FindIn(ApplyTreeValues(tree, bad_name).tree, 11), "a malformed glyph name is ignored");

  // A new profile arrives with its Space.
  auto profiled = values;
  const std::string profile = "11111111-2222-4333-8444-555555555555";
  profiled["profile/" + profile] = "{\"name\":\"Personal\"}";
  auto space3 = values.at("space/" + Id(2));
  space3.replace(space3.find("\"profile\":\"default\""), 19, "\"profile\":\"" + profile + "\"");
  space3.replace(space3.find("\"position\":1"), 12, "\"position\":2");
  profiled["space/" + Id(3)] = space3;
  auto with_profile = ApplyTreeValues(tree, profiled);
  Check(with_profile.ok && with_profile.tree.profiles.size() == 2 && with_profile.tree.spaces.size() == 3 &&
        with_profile.tree.spaces[2].profile == profile, "profile created with its space");

  // The store persists a synced tree and refuses a stale one.
  const auto db = TempDir(L"store") / L"workspace.sqlite3";
  red::TreeStore store;
  auto opened = store.Open(db);
  Check(static_cast<bool>(opened), "store opens");
  if (opened) {
    auto next = opened.snapshot;
    next.spaces = tree.spaces; next.active_space = tree.active_space; next.nodes = tree.nodes; next.active_id = tree.active_id;
    auto saved = store.ApplySynced(next, opened.snapshot.revision);
    Check(static_cast<bool>(saved) && saved.snapshot.nodes.size() == tree.nodes.size() && saved.snapshot.revision == opened.snapshot.revision + 1,
          "store applies synced tree");
    auto reread = store.Read();
    Check(static_cast<bool>(reread) && TreeValues(reread.snapshot) == values, "synced tree persisted");
    Check(store.ApplySynced(next, opened.snapshot.revision).code == 409, "stale synced tree refused");
    auto broken = next; broken.spaces.clear();
    Check(store.ApplySynced(broken, saved.snapshot.revision).code == 400, "invalid synced tree refused");
  }
  std::error_code ignored;
  fs::remove_all(db.parent_path(), ignored);
}
}  // namespace

void TestSplits() {
  // Pinned split: Tracker (13) + a second Pinned page (19) in Space 1.
  auto base = SampleTree();
  auto second = Node(19, 6, "Board"); second.pinned = true; second.space = Id(1);
  base.nodes.push_back(second);
  auto pinned_split = red::SplitGroup::Pair(Id(13), Id(19), true, false, 4000);
  pinned_split.title = "Planning";
  auto with_split = base;
  with_split.splits = {pinned_split};
  Check(red::TreeStore::ValidateSnapshot(with_split), "pinned split tree valid");
  const auto values = TreeValues(with_split);
  Check(values.contains("split/" + Id(13)), "pinned split synced");

  // Another PC has the pages but not the split: it gets the split with its layout and title.
  auto received = ApplyTreeValues(base, values);
  Check(received.ok && received.changed && received.tree.splits.size() == 1 && received.tree.splits[0] == pinned_split, "pinned split received");
  Check(ApplyTreeValues(with_split, values).ok && !ApplyTreeValues(with_split, values).changed, "own split changes nothing");

  // Ungrouped elsewhere: the record is gone, so is the split.
  auto ungrouped_values = values;
  ungrouped_values.erase("split/" + Id(13));
  auto ungrouped = ApplyTreeValues(with_split, ungrouped_values);
  Check(ungrouped.ok && ungrouped.tree.splits.empty() && FindIn(ungrouped.tree, 19), "ungrouped split removed, pages kept");

  // A split with a Today page stays on this PC.
  auto local_split = base;
  local_split.splits = {red::SplitGroup::Pair(Id(13), Id(14))};
  Check(red::TreeStore::ValidateSnapshot(local_split), "today split tree valid");
  Check(!TreeValues(local_split).contains("split/" + Id(13)), "today split not synced");
  auto kept = ApplyTreeValues(local_split, TreeValues(base));
  Check(kept.ok && kept.tree.splits.size() == 1 && kept.tree.splits[0].Contains(Id(14)), "today split kept");

  // The same Pinned page split locally with Today and remotely with Pinned: the synced split wins.
  auto conflict = ApplyTreeValues(local_split, values);
  Check(conflict.ok && conflict.tree.splits.size() == 1 && conflict.tree.splits[0] == pinned_split, "synced split wins over local");

  // Damaged split records are ignored.
  auto bad = values;
  bad["split/" + Id(13)] = "{\"pages\":[\"" + Id(13) + "\"],\"title\":\"\",\"layout\":\"0\",\"linked\":false}";
  auto ignored = ApplyTreeValues(base, bad);
  Check(ignored.ok && ignored.tree.splits.empty(), "one-page split record ignored");
  bad["split/" + Id(13)] = "{\"pages\":[\"" + Id(13) + "\",\"" + Id(19) + "\"],\"title\":\"\",\"layout\":\"h(1*0)\",\"linked\":false}";
  Check(ApplyTreeValues(base, bad).ok && ApplyTreeValues(base, bad).tree.splits.empty(), "bad layout ignored");
  bad["split/" + Id(13)] = values.at("split/" + Id(13));
  bad.erase("node/" + Id(19));
  auto missing = ApplyTreeValues(base, bad);
  Check(missing.ok && missing.tree.splits.empty(), "split of a removed page dropped");
}

// One simulated PC: a real workspace store, a password store stand-in and
// its own Sync directory; passes run exactly as Sol will run them.
struct VirtualPc {
  fs::path dir;
  red::TreeStore store;
  LocalSync local;
  OpenedVault vault;
  Values passwords;
  bool fail_password_writes = false;
  bool older_version = false;  // Knows no "future/" records: never holds or offers them.
  bool wrote = false;

  explicit VirtualPc(const fs::path& root) : dir(root) { fs::create_directories(root); store.Open(root / "workspace.sqlite3"); }
  ~VirtualPc() { vault.Close(); WipeValues(passwords); }
  VirtualPc(const VirtualPc&) = delete;
  VirtualPc& operator=(const VirtualPc&) = delete;
  red::TreeSnapshot Tree() { return store.Read().snapshot; }
  Values All() {
    Values values = TreeValues(Tree());
    for (const auto& item : passwords) if (!older_version || !item.first.starts_with("future/")) values.insert(item);
    return values;
  }
  PassStatus Pass(int64_t now) {
    Prepared prepared;
    const auto status = Prepare(local, vault, All(), now, &IsTreeKey, prepared);
    if (status != PassStatus::Ok) return status;
    std::set<std::string> failed;
    // Passwords: one write per key, any of which may fail.
    for (const auto& [key, value] : prepared.changes.upsert)
      if (!IsTreeKey(key) && !(older_version && key.starts_with("future/"))) { if (fail_password_writes) failed.insert(key); else passwords[key] = value; }
    for (const auto& key : prepared.changes.remove)
      if (!IsTreeKey(key)) { if (fail_password_writes) failed.insert(key); else passwords.erase(key); }
    // Tree: all or nothing.
    const bool tree_touched = prepared.use_synced_tree ||
        std::any_of(prepared.changes.upsert.begin(), prepared.changes.upsert.end(), [](const auto& item) { return IsTreeKey(item.first); }) ||
        std::any_of(prepared.changes.remove.begin(), prepared.changes.remove.end(), [](const auto& key) { return IsTreeKey(key); });
    if (tree_touched) {
      Values tree_values;
      for (const auto& [key, value] : prepared.live) if (IsTreeKey(key)) tree_values.emplace(key, value);
      const auto before = Tree();
      const auto applied = ApplyTreeValues(before, tree_values);
      if (!applied.ok || (applied.changed && !store.ApplySynced(applied.tree, before.revision))) {
        for (const auto& [key, value] : prepared.changes.upsert) if (IsTreeKey(key)) failed.insert(key);
        for (const auto& key : prepared.changes.remove) if (IsTreeKey(key)) failed.insert(key);
      }
    }
    return Commit(local, vault, prepared, failed, &wrote);
  }
  bool Edit(const std::function<void(red::TreeSnapshot&)>& change) {
    auto tree = Tree();
    const auto revision = tree.revision;
    change(tree);
    return static_cast<bool>(store.ApplySynced(tree, revision));
  }
};

std::string Pw(const char* site, const char* user) { return std::string("password/default/https://") + site + "/\n" + user; }
std::string PwValue(const char* password) { return std::string("{\"url\":\"https://x.test/\",\"password\":\"") + password + "\",\"note\":\"\"}"; }
std::set<std::string> SpaceNames(VirtualPc& pc) {
  std::set<std::string> names;
  for (const auto& s : pc.Tree().spaces) names.insert(s.name);
  return names;
}
bool Converged(VirtualPc& a, VirtualPc& b) { return a.All() == b.All(); }

// Site icons of synced pages travel with the records; one icon per host, never flipped.
void TestIcons() {
  Check(SyncedIconHost("https://Mail.Example.test:8443/inbox?x#y") == "mail.example.test" && SyncedIconHost("http://a-b.test") == "a-b.test",
        "icon host is the lowercase DNS host");
  for (const char* url : {"file:///C:/a.pdf", "chrome://settings", "https://user@example.test/", "https://[::1]/", "https://ex ample.test/", ""})
    Check(SyncedIconHost(url).empty(), "no icon host for other addresses");
  Check(IconKeyHost("icon/example.test") == "example.test" && IconKeyHost("icon/Example.test").empty() && IconKeyHost("icon/").empty() &&
        IconKeyHost("icon/a/b").empty() && IconKeyHost("node/x").empty() && IsIconKey("icon/x") && !IsTreeKey("icon/x"), "icon keys");
  auto tree = SampleTree();
  auto& nodes = tree.nodes;
  nodes[0].url = "https://mail.test/inbox";      // Favorite
  nodes[2].url = "https://spec.test/doc";        // Pinned child of a folder
  nodes[3].url = "https://tracker.test/";        // Pinned
  nodes[4].url = "https://today-only.test/";     // Today: not synced
  nodes[5].url = "https://archived.test/";       // Archived: not synced
  nodes[6].url = "https://tracker.test/other";   // Pinned in Space 2, same host
  const auto hosts = SyncedIconHosts(tree);
  Check(hosts.size() == 3 && hosts.contains("mail.test") && hosts.contains("spec.test") && hosts.contains("tracker.test") &&
        hosts.at("tracker.test") == std::set<std::string>{"default"}, "icon hosts of Pinned and Favorites only, with their profile");
  const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + "pixels";
  const std::string other = std::string("\x89PNG\r\n\x1a\n", 8) + "other pixels";
  Values base = {{"icon/tracker.test", Base64Encode(other)}, {"icon/gone.test", Base64Encode(png)}};
  Values out;
  AddIconValues(hosts, {{"mail.test", png}, {"tracker.test", png}, {"spec.test", "GIF89a not a png"}}, base, out);
  Check(out.size() == 2 && out.at("icon/mail.test") == Base64Encode(png), "this PC's PNG for a host not synced yet");
  Check(out.at("icon/tracker.test") == Base64Encode(other), "a synced icon keeps its record, not this PC's version");
  Check(!out.contains("icon/spec.test") && !out.contains("icon/gone.test"), "no record without a PNG, and none for unused hosts");
  std::string parsed;
  Check(ParseIconValue(Base64Encode(png), parsed) && parsed == png, "icon record parses");
  Check(!ParseIconValue(Base64Encode("GIF89a"), parsed) && !ParseIconValue("not base64!", parsed) &&
        !ParseIconValue(Base64Encode(std::string("\x89PNG\r\n\x1a\n", 8) + std::string(16 * 1024, 'x')), parsed), "only PNGs up to 16 KiB");
}

void TestVirtualPcs() {
  const auto root = TempDir(L"pcs");
  const auto cloud = root / "OneDrive";
  fs::create_directories(cloud);
  VirtualPc a(root / "A"), b(root / "B"), c(root / "C");
  int64_t now = 1800000000000;

  // PC A: two Spaces with Pinned, Favorites, a folder and a password; creates the vault.
  Check(a.Edit([](red::TreeSnapshot& t) {
    const auto sample = SampleTree();
    t.spaces = sample.spaces; t.active_space = sample.active_space; t.nodes = sample.nodes; t.active_id = sample.active_id;
  }), "A: sample workspace");
  a.passwords[Pw("bank.test", "ann")] = PwValue("hunter2-secret");
  Check(SetUp(a.dir / "Sync", cloud, "correct horse battery", JoinMode::Merge, a.local, a.vault, kFast) == SetupStatus::Ok, "A: creates vault");
  Check(a.Pass(++now) == PassStatus::Ok && a.wrote, "A: first pass writes");
  Check(a.Pass(++now) == PassStatus::Ok && !a.wrote, "A: nothing changed, nothing written");
  Check(a.Pass(++now) == PassStatus::Ok && !a.wrote, "A: still nothing written");

  // PC B joins and merges: it keeps its own default Space and gets A's.
  OpenedVault wrong;
  LocalSync wrong_local;
  Check(SetUp(b.dir / "Sync", cloud, "wrong passphrase!!", JoinMode::Merge, wrong_local, wrong, kFast) == SetupStatus::WrongPassphrase, "B: wrong passphrase refused");
  Check(!IsSetUp(b.dir / "Sync"), "B: nothing saved after a wrong passphrase");
  const auto b_default = SpaceNames(b);
  Check(SetUp(b.dir / "Sync", cloud, "correct horse battery", JoinMode::Merge, b.local, b.vault, kFast) == SetupStatus::Ok, "B: joins");
  Check(b.Pass(++now) == PassStatus::Ok && b.wrote, "B: first pass");
  Check(SpaceNames(b).contains("Work") && SpaceNames(b).contains("Home") && SpaceNames(b).size() == 2 + b_default.size(), "B: merged Spaces");
  Check(b.passwords.contains(Pw("bank.test", "ann")), "B: got A's password");
  Check(b.local.join.empty(), "B: join finished");
  Check(a.Pass(++now) == PassStatus::Ok && Converged(a, b), "A and B converge");

  // PC C joins with "use the synced Spaces": its own default Space is not spread.
  Check(SetUp(c.dir / "Sync", cloud, "correct horse battery", JoinMode::UseSynced, c.local, c.vault, kFast) == SetupStatus::Ok, "C: joins");
  c.passwords[Pw("mail.test", "cat")] = PwValue("c-only");
  Check(c.Pass(++now) == PassStatus::Ok, "C: first pass");
  Check(SpaceNames(c) == SpaceNames(a), "C: has exactly the synced Spaces");
  Check(c.passwords.contains(Pw("bank.test", "ann")) && c.passwords.contains(Pw("mail.test", "cat")), "C: passwords merged, not replaced");
  a.Pass(++now); b.Pass(++now); c.Pass(++now);
  Check(Converged(a, b) && Converged(b, c), "A, B and C converge");
  Check(a.passwords.contains(Pw("mail.test", "cat")), "A: got C's password");

  // A newer Sol's kind of record ("future/") survives an older Sol's passes.
  b.older_version = true;
  a.passwords["future/feature"] = "x";
  a.Pass(++now); b.Pass(++now); b.Pass(++now); a.Pass(++now);
  Check(a.passwords.contains("future/feature") && !b.passwords.contains("future/feature"),
        "an older PC carries a newer PC's records instead of deleting them");
  b.older_version = false;  // B is updated: it now receives the record.
  b.Pass(++now); c.Pass(++now); a.Pass(++now);
  Check(Converged(a, b) && Converged(b, c) && b.passwords.contains("future/feature"), "an updated PC receives a newer PC's records");

  // Concurrent edits while "offline": A renames a Space and adds a Pinned tab,
  // B deletes a Pinned tab and changes a password, C deletes a password.
  a.Edit([](red::TreeSnapshot& t) {
    for (auto& s : t.spaces) if (s.name == "Work") s.name = "Work 2";
    auto added = Node(40, static_cast<int>(std::count_if(t.nodes.begin(), t.nodes.end(), [](const auto& n) { return n.parent.empty(); })), "Added on A");
    added.pinned = true; added.space = Id(1); t.nodes.push_back(added);
  });
  b.Edit([](red::TreeSnapshot& t) {
    std::erase_if(t.nodes, [](const auto& n) { return n.id == Id(13); });
    int position = 0;
    std::vector<red::TreeNode*> roots;
    for (auto& n : t.nodes) if (n.parent.empty()) roots.push_back(&n);
    std::sort(roots.begin(), roots.end(), [](auto x, auto y) { return x->position < y->position; });
    for (auto* n : roots) n->position = position++;
  });
  b.passwords[Pw("bank.test", "ann")] = PwValue("changed-on-b");
  c.passwords.erase(Pw("mail.test", "cat"));
  for (int round = 0; round < 2; ++round) { a.Pass(++now); b.Pass(++now); c.Pass(++now); }
  Check(Converged(a, b) && Converged(b, c), "converge after concurrent edits");
  Check(SpaceNames(c).contains("Work 2") && FindIn(c.Tree(), 40) && !FindIn(c.Tree(), 13) && !FindIn(a.Tree(), 13), "tree edits everywhere");
  Check(a.passwords.at(Pw("bank.test", "ann")) == PwValue("changed-on-b") && !a.passwords.contains(Pw("mail.test", "cat")), "password edits everywhere");
  Check(FindIn(a.Tree(), 14) && !FindIn(b.Tree(), 14), "Today tabs stay on their PC");

  // A password that C cannot store yet is delivered again, never turned into a deletion.
  a.passwords[Pw("shop.test", "ann")] = PwValue("new-on-a");
  a.Pass(++now);
  c.fail_password_writes = true;
  Check(c.Pass(++now) == PassStatus::Ok && !c.passwords.contains(Pw("shop.test", "ann")), "C: write failed");
  c.Pass(++now);
  a.Pass(++now);
  Check(a.passwords.contains(Pw("shop.test", "ann")), "failed write does not delete elsewhere");
  c.fail_password_writes = false;
  Check(c.Pass(++now) == PassStatus::Ok && c.passwords.contains(Pw("shop.test", "ann")), "C: delivered on the next pass");

  // B turns sync off; meanwhile A removes a Pinned tab that B still has. B
  // rejoins with merge: the removal wins, B's stale copy does not come back.
  Check(TurnOff(b.local) && !IsSetUp(b.dir / "Sync"), "B: turned off");
  Check(!fs::exists(SyncDirectory(cloud) / "devices" / (b.local.device + ".sol")), "B: device file removed");
  a.Edit([](red::TreeSnapshot& t) {
    std::erase_if(t.nodes, [](const auto& n) { return n.id == Id(16); });
    int position = 0;
    std::vector<red::TreeNode*> roots;
    for (auto& n : t.nodes) if (n.parent.empty()) roots.push_back(&n);
    std::sort(roots.begin(), roots.end(), [](auto x, auto y) { return x->position < y->position; });
    for (auto* n : roots) n->position = position++;
  });
  a.Pass(++now); c.Pass(++now);
  Check(FindIn(b.Tree(), 16), "B: still has the tab while off");
  Check(SetUp(b.dir / "Sync", cloud, "correct horse battery", JoinMode::Merge, b.local, b.vault, kFast) == SetupStatus::Ok && b.Pass(++now) == PassStatus::Ok,
        "B: rejoins");
  Check(!FindIn(b.Tree(), 16) && !FindIn(a.Tree(), 16), "rejoin does not revive removed tabs");
  a.Pass(++now); c.Pass(++now);
  Check(Converged(a, b) && Converged(b, c), "converge after rejoin");

  // Reloading the saved setup works; the passphrase is not needed again.
  LocalSync reloaded; OpenedVault reloaded_vault;
  Check(Load(a.dir / "Sync", reloaded, reloaded_vault) == LoadStatus::Ok && reloaded.device == a.local.device && reloaded_vault.key == a.vault.key,
        "A: setup reloads without the passphrase");
  reloaded_vault.Close();

  // Nothing readable leaks into the cloud folder.
  bool clean = true;
  for (const auto& entry : fs::recursive_directory_iterator(SyncDirectory(cloud))) {
    std::string text;
    if (entry.is_regular_file() && ReadFileLimited(entry.path(), 1 << 24, text))
      clean = clean && text.find("hunter2") == std::string::npos && text.find("changed-on-b") == std::string::npos &&
              text.find("bank.test") == std::string::npos && text.find("Work") == std::string::npos;
  }
  Check(clean, "cloud folder holds no readable passwords, sites or names");

  // Someone replaced the vault: passes stop instead of writing into it.
  fs::remove_all(SyncDirectory(cloud));
  OpenedVault other;
  CreateVault(SyncDirectory(cloud), "another passphrase!!", other, kFast);
  Check(a.Pass(++now) == PassStatus::VaultChanged, "replaced vault detected");
  fs::remove_all(cloud);
  Check(a.Pass(++now) == PassStatus::FolderMissing, "missing folder detected");

  // Turning sync off while the folder is away (its cloud app quit): this PC's
  // file is deleted once the folder is back, and nobody else's.
  Check(TurnOff(c.local) && !IsSetUp(c.dir / "Sync"), "turning off with the folder away still turns off");
  Check(!RemovePendingDeviceFile(c.dir / "Sync"), "the device file waits while the folder is away");
  fs::create_directories(SyncDirectory(cloud) / "devices");
  const auto orphan = SyncDirectory(cloud) / "devices" / (c.local.device + ".sol");
  const auto other_pc = SyncDirectory(cloud) / "devices" / (a.local.device + ".sol");
  std::ofstream(orphan) << "x";
  std::ofstream(other_pc) << "x";
  Check(RemovePendingDeviceFile(c.dir / "Sync") && !fs::exists(orphan) && fs::exists(other_pc), "the left file is removed when the folder is back");
  Check(RemovePendingDeviceFile(c.dir / "Sync"), "nothing pending afterwards");
  std::error_code ignored;
  fs::remove_all(root, ignored);
}

void TestPasswords() {
  // Keys and values.
  std::string profile, realm, user, url, password;
  const auto key = PasswordKey("default", "https://a.test/", "ann\nsecond line");
  Check(ParsePasswordKey(key, profile, realm, user) && profile == "default" && realm == "https://a.test/" && user == "ann\nsecond line", "password key round trip");
  Check(!ParsePasswordKey("password/../x\nu", profile, realm, user) && !ParsePasswordKey("node/x", profile, realm, user), "bad password keys refused");
  Check(ParsePasswordValue(PasswordValue("https://a.test/login", "p\"w\\1"), url, password) && url == "https://a.test/login" && password == "p\"w\\1",
        "password value round trip");
  Check(!ParsePasswordValue("{\"url\":\"x\"}", url, password) && !ParsePasswordValue("{\"url\":\"x\",\"password\":\"\"}", url, password), "empty password refused");

  // A Sol profile root with the importer's Chromium fixture as its Default store
  // (key bytes 0..31, wrapped with DPAPI for this Windows user).
  const auto root = TempDir(L"passwords");
  fs::create_directories(root / "Default");
  fs::copy_file(fs::path(RED_SYNC_FIXTURES) / "chromium" / "Login Data", root / "Default" / "Login Data");
  std::string master(32, '\0');
  for (int i = 0; i < 32; ++i) master[i] = static_cast<char>(i);
  DATA_BLOB in{32, reinterpret_cast<BYTE*>(master.data())}, wrapped{};
  CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &wrapped);
  const std::string blob = "DPAPI" + std::string(reinterpret_cast<const char*>(wrapped.pbData), wrapped.cbData);
  LocalFree(wrapped.pbData);
  AtomicWrite(root / "Local State", "{\"os_crypt\":{\"encrypted_key\":\"" + Base64Encode(blob) + "\"}}");

  auto read = ReadPasswords(root, "default");
  Check(read.status == PasswordReadStatus::Ok && read.values.size() == 2 && read.skipped == 5, "fixture: two sign-ins, five skipped");
  const auto alice = read.values.find(PasswordKey("default", "https://login.example.test/", "alice@example.test"));
  Check(alice != read.values.end() && ParsePasswordValue(alice->second, url, password) && url == "https://login.example.test/signin" &&
        password == "correct-horse-1", "fixture: password decrypted with its realm and url");
  Check(ReadPasswords(root, "11111111-2222-4333-8444-555555555555").status == PasswordReadStatus::NoStore, "profile without a store");
  Check(ReadPasswords(root, "../evil").status == PasswordReadStatus::Damaged, "bad profile id refused");

  // Chromium keeps the store locked: reading still works from a copy.
  sqlite3* holder = nullptr;
  const auto store = (root / "Default" / "Login Data").u8string();
  sqlite3_open(reinterpret_cast<const char*>(store.c_str()), &holder);
  sqlite3_exec(holder, "PRAGMA locking_mode=EXCLUSIVE; BEGIN EXCLUSIVE; UPDATE logins SET action_url=action_url; COMMIT;", nullptr, nullptr, nullptr);
  Check(ReadPasswords(root, "default").status == PasswordReadStatus::Ok, "readable while another process holds the lock");
  // A write in flight leaves a hot journal: refused as Busy, retried later.
  sqlite3_exec(holder, "BEGIN EXCLUSIVE; UPDATE logins SET action_url='x';", nullptr, nullptr, nullptr);
  Check(ReadPasswords(root, "default").status == PasswordReadStatus::Busy, "busy during a write");
  sqlite3_exec(holder, "ROLLBACK;", nullptr, nullptr, nullptr);
  sqlite3_close(holder);
  AtomicWrite(root / "Local State", "{\"os_crypt\":{\"encrypted_key\":\"bm9wZQ==\"}}");
  Check(ReadPasswords(root, "default").status == PasswordReadStatus::KeyUnavailable, "no key, no passwords");
  std::error_code ignored;
  fs::remove_all(root, ignored);
}

int main(int argc, char** argv) {
  // `sync_test --kdf-time`: how long the production key derivation takes here.
  if (argc == 2 && std::string_view(argv[1]) == "--kdf-time") {
    std::string key;
    const auto start = GetTickCount64();
    const bool ok = Argon2id("a long sync passphrase", std::string(16, 's'), KdfParams{}, 32, key);
    std::printf("argon2id 64 MiB t=3 p=4: %s in %llu ms\n", ok ? "ok" : "failed", GetTickCount64() - start);
    return ok ? 0 : 1;
  }
  TestTree();
  TestSplits();
  TestIcons();
  TestVirtualPcs();
  TestPasswords();
  TestArgon2();
  TestAead();
  TestCompression();
  TestMerge();
  TestSerialize();
  TestVault();
  TestDetection();
  if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
  std::puts("sync_test: all checks passed");
  return 0;
}
