# thyme storage ABI evidence (2026-10-26)

`package/thyme-storage-abi.json` records ABI qualification, not block publication or root mounting.

- Donor: iPad7,1 17.7.11 / 21H461; decompressed SHA-256 `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`.
- Static reference: [macOS run 40259996726](https://github.com/Coryyi/UniXNU/actions/runs/40259996726), public IOStorageFamily revision `e45fbe30a7879e0b16e26dddb78538f1b00e0bff`. The compiled reference has 190 dispatch slots, 176 kernel bindings and class size 0x90.
- Runtime candidate: source kernel `8fbd13fa03d05f38009f72d7a464ad9f9fe8eb43d3cd9d1acb16a195ecb19a2c`, prelinked with IOStorageFamily only, candidate SHA-256 `dfd638967a6d2aed6c0bdb5e879e2558d20a31d2180162ac9bfebac418adf2b8`.
- The original storage executable and slide are identical between static reference and runtime candidate. The qualification tool rechecks every pointer against the actual candidate's exports.

The bounded `bsd_init` probe reads real TCR, SCTLR, TTBR0/1 and the original block/source-service metaclass fields. All seven positive checks pass. Its reset timing is 21.482 seconds versus the 21.565-second baseline. Changing only the expected block class size from 0x90 to 0x91 and using one-second failure steps produces 27.413 seconds: a 5.931-second difference against the required six seconds. The verifier checks that the compared code differs only in this comparison and the timer step constant; each kernel differs from its input only in the bounded probe, branch and resealed source-crypto HMAC. This is timing-based validation, not a raw memory snapshot.

Without explicitly loading IOStorageFamily, the parent-field check fails (approximately 20 seconds beyond baseline). Calling the normal `OSKextLoadKextWithIdentifier` API succeeds and establishes the correct parent and 0x90/0x88 class sizes. The native-UFS adapter now requests this load before using the original class. [Apple API declaration](https://github.com/apple-oss-distributions/xnu/blob/main/libkern/libkern/OSKextLib.h).

An earlier 27-second negative wait returned outside its predicted interval and is excluded from qualification. The shorter negative challenge above is the accepted comparison.

All phone operations used `fastboot boot`. No partition writes were performed. NativeUfsBlock compilation, callback reads, root mounting and PID 1 require further verification.
