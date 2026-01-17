import wave

hex_file = "audio_dump.txt"
wav_file = "grabacion_esp32.wav"

print(f"Leyendo {hex_file}...")
with open(hex_file, "r") as f:
    # Filtramos solo las líneas que parecen hexadecimal
    hex_data = "".join(line.strip() for line in f if len(line.strip()) > 30)

print(f"Convirtiendo {len(hex_data)//2} bytes...")
pcm_data = bytes.fromhex(hex_data)

with wave.open(wav_file, "wb") as w:
    w.setnchannels(1)      # Mono
    w.setsampwidth(2)      # 16-bit
    w.setframerate(44100)  # 44.1kHz
    w.writeframes(pcm_data)

print(f"¡Hecho! Archivo '{wav_file}' creado exitosamente.")