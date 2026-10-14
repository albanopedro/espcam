// Mapa de pinos da câmera para a placa "AI Thinker ESP32-CAM".
// Cada constante diz em qual GPIO do ESP32 está ligado um fio do sensor (OV2640).

#define PWDN_GPIO_NUM  32   // liga/desliga a câmera
#define RESET_GPIO_NUM -1   // não usado nessa placa
#define XCLK_GPIO_NUM   0   // relógio que o ESP32 fornece ao sensor

#define SIOD_GPIO_NUM  26   // I2C (SCCB) dados: usado para configurar o sensor
#define SIOC_GPIO_NUM  27   // I2C (SCCB) clock

// Y2..Y9 = os 8 fios por onde os pixels chegam
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5

#define VSYNC_GPIO_NUM 25   // sinal de "começou um novo quadro"
#define HREF_GPIO_NUM  23   // sinal de "começou uma nova linha"
#define PCLK_GPIO_NUM  22   // relógio dos pixels
