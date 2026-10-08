# SM8250 encrypted userdata compatibility

Status: experimental legacy implementation builds; physical key programming
and existing-data compatibility are unverified. No userdata has been changed.

The actual build output inspected was
/home/aosp/infx/out/target/product/pipa/vendor/etc/fstab.qcom. It uses F2FS,
inlinecrypt, fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized+wrappedkey_v0,
and metadata_encryption=aes-256-xts:wrappedkey_v0. This matches the A/B source
fstab_AB.qcom. The non-A/B fstab.qcom has older fileencryption=ice options and
must not be mistaken for the active pipa configuration.

The dm-default-key target and exclusion of encrypted file-content bios from
metadata encryption are now present and VM-tested with raw software keys.
That resolves one layer of the storage dependency, not existing-data access.

## Legacy secure firmware ABI versus Linux 6.18

| Operation | Working downstream 4.19 path | Current 6.18 path |
| --- | --- | --- |
| Program key | ES 0x05: slot, shared key address, key size, cipher, data-unit mask, storage type | Experimental matching wrapper; existing ES 0x04 path retained |
| Evict key | ES 0x06: slot, storage type | Experimental matching wrapper; existing ES 0x03 path retained |
| Derive software secret | ES 0x07 via QTEE shared buffers | ES 0x07 via qcom_tzmem buffers |
| Wrapped key support | Legacy Qualcomm key wrapping; UFS storage type 10 | Separate legacy pipa UFS path plus existing HWKM v2 support |

Downstream references are drivers/soc/qcom/crypto-qti-tz.[ch],
drivers/soc/qcom/crypto-qti-common.c and
drivers/scsi/ufs/ufshcd-crypto-qti.c. Wrapped key bytes are sent unchanged;
raw AES-XTS key words are converted to big endian. The derive-secret helper
also has a legacy short-key case that copies the software-secret prefix for
keys of at most 64 bytes. This behavior needs deliberate compatibility review.

The existing HWKM v2 path still requires modern firmware, effectively SM8650
or later. A separate QCOM_ICE_SM8250_LEGACY_WRAPPED_KEYS option now implements
program/evict/derive for pipa's dedicated UFS ICE node, gated by machine,
ICE compatible and availability of all three legacy SCM calls. Other SoCs
keep their existing behavior. The modern generate/prepare/import operations
are removed from the legacy UFS profile; wrappedkey_v0 continues using
Android Keymaster to prepare keys. Wrapped-key programming failures attempt
to invalidate the potentially partially programmed slot.

The pipa DT now exposes ICE at 0x01d90000 (size 0x8000), with the UFS PHY ICE
clock assigned to 300 MHz, matching Xiaomi's kona source. UFS references that
engine. The ICE binding and its application to the compiled pipa DTB pass
targeted dt-schema checks.

Android's legacy fscrypt add-key flag at byte offset 76 is also supported,
using Android common revision 9d29ba85d8a2c505b2d049568c3b1d29c4e0c2fd.
It selects HKDF key-identifier context 1 to preserve the old on-disk format;
the upstream flag keeps context 8. Unknown flags, conflicting formats and
nonzero reserved words are rejected. Both recognized flag formats reach
the expected unsupported-hardware error in the disposable VM, and all four
raw-key filesystem round trips still pass. These tests do not validate an
actual hardware-derived identifier or decrypt existing userdata.

Modern qcom_tzmem already implements the shared-memory bridge transport used
by secure firmware. The pipa build selects QCOM_TZMEM_MODE_SHMBRIDGE. Its
buffer lifetime, cache coherency, firmware return values, key erasure and
ownership rules remain important for firmware validation. The wrappers use
its DMA-coherent, bridge-registered pool, wipe shared keys before freeing,
and retain the SCM transport's error handling. No old allocator was copied.

## Required implementation and evidence

1. Validate implemented legacy SCM calls and key-format behavior against
   disposable data on pipa, including error paths and slot eviction.
2. Verify data-unit, slot and key-format semantics across suspend/resume.
3. Verify vold/Keymaster and fscrypt v2 IV_INO_LBLK_64 end to end.
4. After the above succeeds, verify
   existing-data compatibility and recovery. VM software encryption cannot
   establish firmware compatibility.

Do not change encryption options, silently fall back to a different key
format, format userdata or claim existing-data support to bypass this blocker.
