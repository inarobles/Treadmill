# Flujo de Trabajo Simple con Git

## ✅ Estado Actual

**Rama principal recomendada**: `claude/view-git-branches-011CUxhxBHEktGFaKtcPgMVh`

Esta rama tiene:
- ✅ Protocolo UART ASCII (reemplaza RS485-Modbus)
- ✅ Corrección del crash por mutex no inicializado
- ✅ Archivo `dependencies.lock` ignorado para evitar conflictos
- ✅ Código compilado y funcionando

## 📋 Cómo Trabajar de Forma Simple

### 1️⃣ Primera vez: Descargar el código actualizado

```bash
cd C:\esp\Treadmill
git fetch origin
git checkout claude/view-git-branches-011CUxhxBHEktGFaKtcPgMVh
git pull origin claude/view-git-branches-011CUxhxBHEktGFaKtcPgMVh
```

### 2️⃣ Para compilar

```bash
cd C:\esp\Treadmill\Consola
idf.py build

cd C:\esp\Treadmill\Base
idf.py build
```

### 3️⃣ Para flashear

```bash
cd C:\esp\Treadmill\Consola
idf.py flash monitor
```

### 4️⃣ Si quieres ver tus cambios locales

```bash
git status          # Ver qué archivos cambiaron
git diff            # Ver exactamente qué cambió
```

## 🚫 Ramas que puedes ignorar

Estas ramas están desactualizadas o tienen problemas:
- ❌ `claude/cinta-uart-011CUxhxBHEktGFaktcPgMVh` (nombre con typo)
- ❌ Otras ramas antiguas

## 💡 Regla de Oro

**Trabaja siempre desde: `claude/view-git-branches-011CUxhxBHEktGFaKtcPgMVh`**

Si necesitas hacer cambios, háblame y yo me encargo de crear las ramas necesarias y fusionarlas.

## 🔧 Solución de Problemas

### Si el proyecto no compila por `dependencies.lock`:
```bash
# Borra el archivo local
del dependencies.lock

# Reconfigurar proyecto
idf.py reconfigure
```

### Si ves errores de "mutex no inicializado":
Asegúrate de estar en la rama correcta:
```bash
git branch              # Debe mostrar: * claude/view-git-branches-011CUxhxBHEktGFaKtcPgMVh
git log --oneline -1    # Debe mostrar: d4de040 Corrige crash por acceso a mutex no inicializado
```

## 📌 Resumen: Tres Comandos Principales

```bash
# 1. Bajar cambios nuevos
git pull origin claude/view-git-branches-011CUxhxBHEktGFaKtcPgMVh

# 2. Compilar
idf.py build

# 3. Flashear
idf.py flash monitor
```

¡Eso es todo! No necesitas preocuparte por ramas, merges, ni nada complicado. 🎉
