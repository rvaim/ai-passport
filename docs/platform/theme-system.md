<p align="right"><strong>English</strong> · <a href="theme-system.zh_CN.md">简体中文</a></p>

# Lightweight Theme System

Themes use the same `.pap` transport and installer with `type: theme`, but execute no code. V1 exposes only background, surface, text, muted text, accent, divider, spacing, and radius tokens.

System pages read these tokens when created, so standard app UI inherits the active theme. Themes cannot replace the shared Chinese font, change hardware key semantics, or inject scripts.
