# Campaign C reference images

Reference PNGs are deliberately **not** generated or updated by normal regression runs.
They must be captured on an explicitly recorded Windows/D3D12 baseline and promoted with
`scripts/update-campaign-c-references.ps1 -ConfirmReferenceUpdate` after human review.
Each PNG is accompanied by the capture metadata JSON emitted by Daedalus. The normal
regression script writes candidates and difference images under `tests/rendering/results/`
and fails when a reference is absent or a configured threshold is exceeded.

Comparisons convert the stored sRGB PNG RGB channels back to linear sRGB before computing
MAE, RMSE, maximum absolute error, and the fraction of channels above the per-channel
threshold. Alpha is not part of the Campaign C radiometric metric.
