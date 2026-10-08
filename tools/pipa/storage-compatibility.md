# SM8250 encrypted userdata compatibility

Status: blocked for the existing Android ROM; no userdata changes authorized
or needed for this investigation.

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
| Program key | ES 0x05: slot, shared key address, key size, cipher, data-unit mask, storage type | ES 0x04: no storage-type argument |
| Evict key | ES 0x06: slot, storage type | ES 0x03: slot |
| Derive software secret | ES 0x07 via QTEE shared buffers | ES 0x07 via qcom_tzmem buffers |
| Wrapped key support | Legacy Qualcomm key wrapping; UFS storage type 10 | HWKM v2 path, requiring additional firmware calls 0x08/0x09/0x0a |

Downstream references are drivers/soc/qcom/crypto-qti-tz.[ch],
drivers/soc/qcom/crypto-qti-common.c and
drivers/scsi/ufs/ufshcd-crypto-qti.c. Wrapped key bytes are sent unchanged;
raw AES-XTS key words are converted to big endian. The derive-secret helper
also has a legacy short-key case that copies the software-secret prefix for
keys of at most 64 bytes. This behavior needs deliberate compatibility review.

In this tree, drivers/soc/qcom/ice.c explicitly limits its wrapped-key path
to HWKM v2 with the necessary firmware interfaces, effectively SM8650 or later.
The qcom_ice.use_wrapped_keys parameter therefore cannot enable SM8250's
legacy path. Advertising wrapped-key support without implementing it would
leave Android unable to decrypt /data.

Modern qcom_tzmem already implements the shared-memory bridge transport used
by secure firmware. The pipa build selects QCOM_TZMEM_MODE_SHMBRIDGE. Its
buffer lifetime, cache coherency, firmware return values, key erasure and
ownership rules must be checked when adding legacy ES 0x05/0x06 wrappers;
copying the old allocator or changing SCM command numbers alone is inadequate.

## Required implementation and evidence

1. Add explicit legacy SCM wrappers with the documented argument layout and
   correct secure shared buffers. Gate support on matching platform and
   firmware capabilities, leaving existing raw-key/HWKM consumers intact.
2. Integrate legacy program/evict/derive operations into ICE and UFS without
   falsely exposing modern generate/prepare/import operations. Preserve
   data-unit, slot and key-format semantics across suspend and resume.
3. Check vold's existing wrappedkey_v0 preparation path and fscrypt v2
   IV_INO_LBLK_64 policy against the resulting kernel capabilities.
4. Validate against disposable data on actual SM8250 hardware, then verify
   existing-data compatibility and recovery. VM software encryption cannot
   establish firmware compatibility.

Do not change encryption options, silently fall back to a different key
format, format userdata or claim existing-data support to bypass this blocker.
