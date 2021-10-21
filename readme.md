## PooledString Overflow Fixer
Fix CS:GO CUtlRBTree GamePoolString Overflow.
Tested on windows.

### Context
- https://discord.com/channels/223673175571955712/420095739562295296/896737256088408134
- CS:GO alloc pooled string for each templated entity with vscript and every vscript EntFire action, and not free after use.