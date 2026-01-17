import numpy as np
import matplotlib.pyplot as plt

# --- CONFIGURACIÓN ---
# Asegúrate de que tu archivo se llame exactamente así y esté en la misma carpeta
archivo_datos = 'audio_dump.txt' 

def procesar_audio_a_imagen():
    try:
        # 1. Leer el archivo y limpiar el contenido
        print("Abriendo archivo...")
        with open(archivo_datos, 'r') as f:
            contenido = f.read()
            # Quitamos espacios, saltos de línea y retornos de carro
            datos_hex = contenido.replace(' ', '').replace('\n', '').replace('\r', '')

        # 2. Convertir de texto Hexadecimal a valores numéricos
        # Cada par de caracteres hex es un byte; usamos int16 (2 bytes por muestra)
        print("Convirtiendo datos...")
        datos_binarios = bytes.fromhex(datos_hex)
        muestras = np.frombuffer(datos_binarios, dtype=np.int16)

        # 3. Crear la imagen de la Onda (Waveform)
        plt.figure(figsize=(15, 5))
        plt.plot(muestras, color='blue', linewidth=0.5)
        plt.title('Representación Visual: Forma de Onda del Sonido')
        plt.xlabel('Tiempo (Muestras)')
        plt.ylabel('Amplitud')
        plt.grid(True, alpha=0.3)
        
        # Guardamos la primera imagen
        plt.savefig('mi_onda_sonora.png')
        print("¡Imagen de onda guardada como 'mi_onda_sonora.png'!")

        # 4. Crear el Espectrograma (Frecuencias)
        plt.figure(figsize=(15, 7))
        plt.specgram(muestras, Fs=44100, cmap='inferno')
        plt.title('Análisis de Frecuencias: Espectrograma')
        plt.xlabel('Tiempo (s)')
        plt.ylabel('Frecuencia (Hz)')
        plt.colorbar(label='Intensidad (dB)')
        
        # Guardamos la segunda imagen
        plt.savefig('mi_espectrograma.png')
        print("¡Espectrograma guardado como 'mi_espectrograma.png'!")
        
        plt.show()

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    procesar_audio_a_imagen()