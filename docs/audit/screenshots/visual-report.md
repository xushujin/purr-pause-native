# Visual capture report

Capture validation checks fixture data, dimensions and selected fields. Pixel, font and geometry differences are reported without masking or a parity pass threshold.

| Page | Mean absolute RGB error (0–255) | Pixels with any channel difference > 8 |
| --- | ---: | ---: |
| settings | 5.8705 | 8.64% |
| message-settings | 6.5382 | 11.66% |
| message-list | 10.9093 | 14.33% |
| activation | 2.7891 | 4.12% |
| rules | 5.3103 | 8.63% |

Complete field values, geometry, font properties and raw differences are in `visual-report.json`. Exact pixel identity: false.
