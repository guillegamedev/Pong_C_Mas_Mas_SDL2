//1)==================================================LIBRERIAS==================================================
#include <chrono>
#include <SDL2/SDL.h> //funciones básicas de SDL (ventana, eventos, renderizado).
#include <SDL2/SDL_image.h> //carga imágenes en formatos PNG, JPG, etc.
#include <SDL2/SDL_mixer.h> //reproduce sonidos y música.
#include <SDL2/SDL_ttf.h> //dibuja texto con fuentes TrueType.
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <stdlib.h>
#include <windows.h> // sleep

//2)==================================================CONSTANTES==================================================
//define tamanio ventana
const int WINDOW_WIDTH = 1920;
const int WINDOW_HEIGHT = 1080;
//define tamanio pelota + velocidad
const int BALL_WIDTH = 50;
const int BALL_HEIGHT = 50;
const float BALL_SPEED = 0.7f;
//define tamanio paleta + velocidad
const int PADDLE_WIDTH = 60;
const int PADDLE_HEIGHT = 170;
const float PADDLE_SPEED = 1.0f;
//define máximo de goles, tiempo máximo juego y margen de reacción de la CPU
const int MAX_GOALS = 7;
const int MAX_TIME_SECONDS = 120;
const float CPU_MARGIN = 200.0f;

//3)==================================================Enumeraciones==================================================
//para saber qué teclas controla la paleta del jugador.
enum Buttons { PaddleOneUp = 0, PaddleOneDown };
//define tipos de colisión de la pelota (pared, paleta, partes de la paleta).
enum class CollisionType { None, Top, Middle, Bottom, Left, Right };
//estados del juego
enum class GameState { Main, Gameplay, Result };

//4)==================================================Struct==================================================
//guarda el tipo de colisión y cuánto se “metió” la pelota en el objeto.
struct Contact { CollisionType type; float penetration; };
//estructura para guardar puntuaciones y tiempo en un archivo CSV.
struct ScoreEntry { std::string name; int score; int timeSec; };

//==================================================Clases==================================================
//Representa vectores 2D para posición y velocidad (Soporta suma y multiplicación por escalar)
class Vec2 {
public:
    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(float x, float y) : x(x), y(y) {}
    Vec2 operator+(Vec2 const& rhs) { return Vec2(x + rhs.x, y + rhs.y); }
    Vec2& operator+=(Vec2 const& rhs) { x += rhs.x; y += rhs.y; return *this; }
    Vec2 operator*(float rhs) { return Vec2(x * rhs, y * rhs); }
    float x, y;
};
//Representa la pelota.
//Update: mueve la pelota según su velocidad.
//Draw: dibuja la pelota usando SDL_Texture.
//CollideWithPaddle y CollideWithWall: detecta colisiones y rebota la pelota.
//Reset: reposiciona la pelota en el centro después de un gol.
class Ball {
public:
    Ball(Vec2 position, Vec2 velocity, SDL_Texture* tex) : position(position), velocity(velocity), texture(tex) {
        rect = { (int)position.x, (int)position.y, BALL_WIDTH, BALL_HEIGHT };
    }
    void Update(float dt) { position += velocity * dt; rect.x = (int)position.x; rect.y = (int)position.y; }
    void Draw(SDL_Renderer* renderer) { SDL_RenderCopy(renderer, texture, nullptr, &rect); }
    void CollideWithPaddle(Contact const& contact) {
        position.x += contact.penetration;
        velocity.x = -velocity.x;
        if (contact.type == CollisionType::Top) velocity.y = -0.75f * BALL_SPEED;
        else if (contact.type == CollisionType::Bottom) velocity.y = 0.75f * BALL_SPEED;
    }
    void CollideWithWall(Contact const& contact) {
        if (contact.type == CollisionType::Top || contact.type == CollisionType::Bottom) {
            position.y += contact.penetration; velocity.y = -velocity.y;
        }
        else if (contact.type == CollisionType::Left) Reset(false);
        else if (contact.type == CollisionType::Right) Reset(true);
    }
    void Reset(bool leftServe) {
        position = Vec2(WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f);
        velocity.x = leftServe ? -BALL_SPEED : BALL_SPEED;
        velocity.y = 0.75f * BALL_SPEED;
    }
    Vec2 position; Vec2 velocity; SDL_Rect rect{};
private:
    SDL_Texture* texture;
};

//Representa las paletas (jugador y CPU).
//Update: mueve la paleta y limita su posición dentro de la ventana.
//Draw: dibuja la paleta.
class Paddle {
public:
    Paddle(Vec2 position, Vec2 velocity, SDL_Texture* tex) : position(position), velocity(velocity), texture(tex) {
        rect = { (int)position.x, (int)position.y, PADDLE_WIDTH, PADDLE_HEIGHT };
    }
    void Update(float dt) {
        position += velocity * dt;
        if (position.y < 0) position.y = 0;
        else if (position.y > WINDOW_HEIGHT - PADDLE_HEIGHT) position.y = WINDOW_HEIGHT - PADDLE_HEIGHT;
        rect.y = (int)position.y;
    }
    void Draw(SDL_Renderer* renderer) { SDL_RenderCopy(renderer, texture, nullptr, &rect); }
    Vec2 position; Vec2 velocity; SDL_Rect rect{};
private:
    SDL_Texture* texture;
};

//Representa el marcador en pantalla.
//SetScore: actualiza la puntuación.
//Draw: dibuja el marcador usando SDL_TTF.
class PlayerScore {
public:
    PlayerScore(Vec2 position, SDL_Renderer* renderer, TTF_Font* font) : renderer(renderer), font(font) {
        surface = TTF_RenderText_Solid(font, "0", { 255,255,255,255 });
        texture = SDL_CreateTextureFromSurface(renderer, surface);
        int w, h; SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
        rect = { (int)position.x, (int)position.y, w, h };
    }
    ~PlayerScore() { SDL_FreeSurface(surface); SDL_DestroyTexture(texture); }
    void SetScore(int score, SDL_Color color = {255,255,255,255}) {
        SDL_FreeSurface(surface); SDL_DestroyTexture(texture);
        surface = TTF_RenderText_Solid(font, std::to_string(score).c_str(), color);
        texture = SDL_CreateTextureFromSurface(renderer, surface);
        int w, h; SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
        rect.w = w; rect.h = h;
    }
    void Draw() { SDL_RenderCopy(renderer, texture, nullptr, &rect); }
private:
    SDL_Renderer* renderer; TTF_Font* font; SDL_Surface* surface{}; SDL_Texture* texture{}; SDL_Rect rect{};
};

//6)==================================================Funciones==================================================

//Funcion de colision
//Verifica si la pelota colisiona con paletas o paredes.
//Devuelve un Contact con información sobre la colisión.
Contact CheckPaddleCollision(Ball const& ball, Paddle const& paddle) {
    float ballL = ball.position.x, ballR = ball.position.x + BALL_WIDTH;
    float ballT = ball.position.y, ballB = ball.position.y + BALL_HEIGHT;
    float padL = paddle.position.x, padR = paddle.position.x + PADDLE_WIDTH;
    float padT = paddle.position.y, padB = paddle.position.y + PADDLE_HEIGHT;
    Contact contact{};
    if (ballL >= padR || ballR <= padL || ballT >= padB || ballB <= padT) return contact;
    float rangeU = padB - 2.0f * PADDLE_HEIGHT / 3.0f;
    float rangeM = padB - PADDLE_HEIGHT / 3.0f;
    contact.penetration = (ball.velocity.x < 0) ? padR - ballL : padL - ballR;
    if (ballB > padT && ballB < rangeU) contact.type = CollisionType::Top;
    else if (ballB > rangeU && ballB < rangeM) contact.type = CollisionType::Middle;
    else contact.type = CollisionType::Bottom;
    return contact;
}

//Funcion de colision
//Verifica si la pelota colisiona con paletas o paredes.
//Devuelve un Contact con información sobre la colisión.
Contact CheckWallCollision(Ball const& ball) {
    Contact contact{};
    if (ball.position.x < 0) contact.type = CollisionType::Left;
    else if (ball.position.x + BALL_WIDTH > WINDOW_WIDTH) contact.type = CollisionType::Right;
    else if (ball.position.y < 0) { contact.type = CollisionType::Top; contact.penetration = -ball.position.y; }
    else if (ball.position.y + BALL_HEIGHT > WINDOW_HEIGHT) { contact.type = CollisionType::Bottom; contact.penetration = WINDOW_HEIGHT - (ball.position.y + BALL_HEIGHT); }
    return contact;
}

//Funcion para leer puntuaciones
//Lee un archivo CSV con los resultados anteriores.
//Ordena por puntuación y tiempo, y devuelve los 10 mejores.
std::vector<ScoreEntry> GetTopScores(const std::string& filename) {
    std::vector<ScoreEntry> scores;
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        ScoreEntry entry;
        std::getline(ss, entry.name, ',');
        ss >> entry.score; ss.ignore();
        ss >> entry.timeSec;
        scores.push_back(entry);
    }
    file.close();
    // Ordenar: mayor puntuación → menor tiempo
    std::sort(scores.begin(), scores.end(), [](ScoreEntry a, ScoreEntry b){
        if(a.score != b.score) return a.score > b.score;
        return a.timeSec < b.timeSec;
    });
    if(scores.size() > 10) scores.resize(10);
    return scores;
}

//7)==================================================MAIN==================================================
//Funcion principal
int main(int argc, char** argv) {
    //1)Inicializacion
    //Inicializa SDL, carga imágenes, audio y fuentes.
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);

    //2)Ventana y renderer
    SDL_Window* window = SDL_CreateWindow("Guille_Pong", 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    //3)Cargar recursos

    //Texturas: pelotas, paletas, fondos.
    //Sonidos: rebote con pared y paleta.

    //Fuente TTF para marcadores.
    TTF_Font* scoreFont = TTF_OpenFont("DejaVuSansMono.ttf", 40);
    
    //Sonido rebote con pared
    Mix_Chunk* wallHitSound = Mix_LoadWAV("WallHit.wav");
    //Sonido rebote con paleta
    Mix_Chunk* paddleHitSound = Mix_LoadWAV("PaddleHit.wav");

    //Textura paleta player
    SDL_Surface* paddlePlayerSurf = IMG_Load("paddle_player.png");
    SDL_Texture* paddlePlayerTex = SDL_CreateTextureFromSurface(renderer, paddlePlayerSurf);
    SDL_FreeSurface(paddlePlayerSurf);
    //Textura paleta cpu
    SDL_Surface* paddleCPUSurf = IMG_Load("paddle_cpu.png");
    SDL_Texture* paddleCPUTex = SDL_CreateTextureFromSurface(renderer, paddleCPUSurf);
    SDL_FreeSurface(paddleCPUSurf);
    //Textura pelota
    SDL_Surface* ballSurf = IMG_Load("ball.png");
    SDL_Texture* ballTex = SDL_CreateTextureFromSurface(renderer, ballSurf);
    SDL_FreeSurface(ballSurf);
    //Textura fondo menu
    SDL_Surface* bgMenuSurf = IMG_Load("menu_background.png");
    SDL_Texture* menuBackground = SDL_CreateTextureFromSurface(renderer, bgMenuSurf);
    SDL_FreeSurface(bgMenuSurf);
    //Textura fondo gameplay
    SDL_Surface* bgGameSurf = IMG_Load("gameplay_background.png");
    SDL_Texture* gameplayBackground = SDL_CreateTextureFromSurface(renderer, bgGameSurf);
    SDL_FreeSurface(bgGameSurf);

    //4)Crear objetos del juego
    Ball ball(Vec2(WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f), Vec2(BALL_SPEED, 0), ballTex);

    Paddle paddleOne(Vec2(50, WINDOW_HEIGHT / 2.0f), Vec2(0,0), paddlePlayerTex);

    Paddle paddleTwo(Vec2(WINDOW_WIDTH-100, WINDOW_HEIGHT/2.0f), Vec2(0,0), paddleCPUTex);

    PlayerScore score1(Vec2(WINDOW_WIDTH/4,20), renderer, scoreFont);

    PlayerScore score2(Vec2(3*WINDOW_WIDTH/4,20), renderer, scoreFont);
    


    //5)Bucle principal

    GameState state = GameState::Main;
    bool running = true; bool buttons[2]={}; float dt = 0.0f;
    int playerOneScore=0, playerTwoScore=0;
    auto startGameTime = std::chrono::high_resolution_clock::now();
    std::string playerName = "PLAYER";


    //---1)Inicio del bucle
    //El bucle se ejecuta mientras running sea true.
    while(running){
        //frameStart guarda el tiempo actual para calcular el delta time (dt), que sirve para mover objetos de manera fluida independientemente de la velocidad de la computadora.
        auto frameStart = std::chrono::high_resolution_clock::now();

    //---2)Manejo de eventos
        SDL_Event event;
        //SDL_PollEvent revisa si ocurrió algún evento (teclado, ratón, cierre de ventana).
        //SDL_QUIT → usuario cerró la ventana.
        //SDL_KEYDOWN → tecla presionada.
        //SDL_KEYUP → tecla liberada.
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_QUIT) running=false;
            else if(event.type==SDL_KEYDOWN){
                if(event.key.keysym.sym==SDLK_ESCAPE) running=false;


                //#############Menu principal (state == Main)###########
                //SPACE: inicia el juego.
                //Resetea los puntajes, la pelota y el tiempo.

                if(state==GameState::Main){
                    if(event.key.keysym.sym==SDLK_SPACE){
                        state = GameState::Gameplay;
                        playerOneScore=0; playerTwoScore=0;
                        score1.SetScore(playerOneScore); score2.SetScore(playerTwoScore);
                        ball.Reset(true); startGameTime=std::chrono::high_resolution_clock::now();

                    //Ingresar nombre
                    //Permite escribir el nombre del jugador (máx. 10 caracteres).
                    //BACKSPACE elimina caracteres.
                    } else if(event.key.keysym.sym >= SDLK_a && event.key.keysym.sym <= SDLK_z && playerName.size() < 10){
                        char c = (char)event.key.keysym.sym;
                        playerName.push_back(c);
                    } else if(event.key.keysym.sym==SDLK_BACKSPACE && !playerName.empty()){
                        playerName.pop_back();
                    }
                }

                if(state==GameState::Result && event.key.keysym.sym==SDLK_SPACE) state=GameState::Main;


                //#############Gameplay (state == Gameplay)###########
                //Teclas W/S controlan la paleta del jugador.
                //KeyUp
                //Cuando sueltas la tecla, se pone false para detener el movimiento.
                if(state==GameState::Gameplay){
                    if(event.key.keysym.sym==SDLK_w) buttons[PaddleOneUp]=true;
                    if(event.key.keysym.sym==SDLK_s) buttons[PaddleOneDown]=true;
                }
            } else if(event.type==SDL_KEYUP){
                if(event.key.keysym.sym==SDLK_w) buttons[PaddleOneUp]=false;
                if(event.key.keysym.sym==SDLK_s) buttons[PaddleOneDown]=false;
            }
        }

                                    //2. Actualización de objetos (jugador, CPU, pelota)


//---3)Actualización del juego

        if(state==GameState::Gameplay){
            // Jugador
            //Se calcula la velocidad vertical según teclas.
            //Update(dt) mueve la paleta según velocity y dt.
            //Se asegura de que no salga de la pantalla.


            paddleOne.velocity.y = 0;
            if(buttons[PaddleOneUp]) paddleOne.velocity.y=-PADDLE_SPEED;
            if(buttons[PaddleOneDown]) paddleOne.velocity.y=PADDLE_SPEED;
            paddleOne.Update(dt);

            // CPU simple
            //La CPU sigue la pelota con un margen de error CPU_MARGIN.
            //La paleta se mueve hacia arriba o abajo según la posición de la pelota.
            float paddleCenter = paddleTwo.position.y + PADDLE_HEIGHT/2.0f;
            if(paddleCenter < ball.position.y - CPU_MARGIN) paddleTwo.velocity.y = PADDLE_SPEED;
            else if(paddleCenter > ball.position.y + CPU_MARGIN) paddleTwo.velocity.y = -PADDLE_SPEED;
            else paddleTwo.velocity.y = 0;
            paddleTwo.Update(dt);


            //Pelota
            //Se mueve según su velocidad actual y dt.
            ball.Update(dt);


//---4)Colisiones
//Paletas: invierte velocity.x y ajusta velocity.y según la parte de la paleta (Top/Middle/Bottom).

//Paredes:

//Arriba/Abajo: rebote vertical.

//Izquierda/Derecha: gol, se actualiza el marcador y se reinicia la pelota.

//Reproduce el sonido de rebote con Mix_PlayChannel.

            if(Contact c=CheckPaddleCollision(ball,paddleOne);c.type!=CollisionType::None){ ball.CollideWithPaddle(c); Mix_PlayChannel(-1,paddleHitSound,0);}
            else if(Contact c=CheckPaddleCollision(ball,paddleTwo);c.type!=CollisionType::None){ ball.CollideWithPaddle(c); Mix_PlayChannel(-1,paddleHitSound,0);}
            else if(Contact c=CheckWallCollision(ball);c.type!=CollisionType::None){
                if(c.type==CollisionType::Left){playerTwoScore++; score2.SetScore(playerTwoScore,{255,0,0,255}); ball.Reset(true);}
                else if(c.type==CollisionType::Right){playerOneScore++; score1.SetScore(playerOneScore,{0,255,0,255}); ball.Reset(false);}
                else ball.CollideWithWall(c);
            }
                        // 4. Dibujar escena
                        // 5. Calcular delta time (dt)


//---5)Comprobación de tiempo / fin de juego
//Calcula tiempo transcurrido.
//Si se alcanza máximo de tiempo o goles, cambia a Result y guarda resultados.
            auto now = std::chrono::high_resolution_clock::now();
            int elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(now-startGameTime).count();
            if(elapsedSec>=MAX_TIME_SECONDS || playerOneScore>=MAX_GOALS || playerTwoScore>=MAX_GOALS){
                state = GameState::Result;
                std::ofstream file("Resultados.csv", std::ios::app);
                file<<playerName<<","<<playerOneScore<<","<<elapsedSec<<"\n"; file.close();
            }
        }

        //=============================DRAW=============================
//---6)Renderizado
//Limpia pantalla.

//Luego dibuja según state:

//Main → menú, nombre y top scores.

//Gameplay → fondo, línea central, paletas, pelota, marcador, tiempo.

//Result → mensaje ganador.
        SDL_SetRenderDrawColor(renderer, 0x3b,0x3b,0x3b,0xFF);
        SDL_RenderClear(renderer);

        if(state==GameState::Main){
            if(menuBackground) SDL_RenderCopy(renderer, menuBackground, nullptr, nullptr);

            SDL_Surface* textSurf = TTF_RenderText_Solid(scoreFont,"PONG - SPACE para jugar",{255,255,255,255});
            SDL_Texture* textTex = SDL_CreateTextureFromSurface(renderer,textSurf);
            SDL_Rect r{WINDOW_WIDTH/4, WINDOW_HEIGHT/8, textSurf->w, textSurf->h};
            SDL_RenderCopy(renderer,textTex,nullptr,&r);
            SDL_DestroyTexture(textTex); SDL_FreeSurface(textSurf);

            std::string nameText = "Nombre: " + playerName;
            SDL_Surface* nameSurf = TTF_RenderText_Solid(scoreFont,nameText.c_str(),{255,255,0,255});
            SDL_Texture* nameTex = SDL_CreateTextureFromSurface(renderer,nameSurf);
            SDL_Rect nr{WINDOW_WIDTH/4, WINDOW_HEIGHT/8 + 50, nameSurf->w, nameSurf->h};
            SDL_RenderCopy(renderer,nameTex,nullptr,&nr);
            SDL_DestroyTexture(nameTex); SDL_FreeSurface(nameSurf);

            auto topScores = GetTopScores("Resultados.csv");
            int yOffset = WINDOW_HEIGHT/4 + 150;
            int rank = 1;
            for(auto& entry : topScores){
                SDL_Color color = {255,255,0,255};
                if(rank == 1) color = {255,215,0,255};
                else if(rank == 2) color = {192,192,192,255};
                else if(rank == 3) color = {205,127,50,255};
                if(entry.name == playerName) color = {0,255,0,255};

                int fontSize = (rank == 1) ? 50 : 40;
                TTF_Font* rankFont = TTF_OpenFont("DejaVuSansMono.ttf", fontSize);
                std::string t = std::to_string(rank) + ". " + entry.name + " - " + std::to_string(entry.score) + " pts - " + std::to_string(entry.timeSec) + " s";
                SDL_Surface* s = TTF_RenderText_Solid(rankFont, t.c_str(), color);
                SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, s);
                SDL_Rect rr{WINDOW_WIDTH/4, yOffset, s->w, s->h};
                SDL_RenderCopy(renderer, tx, nullptr, &rr);
                SDL_DestroyTexture(tx);
                SDL_FreeSurface(s);
                TTF_CloseFont(rankFont);
                yOffset += 60;
                rank++;
            }

        } else {
            if(gameplayBackground) SDL_RenderCopy(renderer, gameplayBackground, nullptr, nullptr);

            SDL_SetRenderDrawColor(renderer, 0xFF,0xFF,0xFF,0xFF);
            for(int y=0;y<WINDOW_HEIGHT;y+=5) SDL_RenderDrawPoint(renderer,WINDOW_WIDTH/2,y);

            ball.Draw(renderer); paddleOne.Draw(renderer); paddleTwo.Draw(renderer);

            score1.Draw(); score2.Draw();

            if(state==GameState::Gameplay){
                SDL_Surface* timeSurf = TTF_RenderText_Solid(scoreFont,std::to_string(MAX_TIME_SECONDS - std::min(MAX_TIME_SECONDS,(int)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - startGameTime).count())).c_str(),{255,255,255,255});
                SDL_Texture* timeTex = SDL_CreateTextureFromSurface(renderer,timeSurf);
                SDL_Rect tr{WINDOW_WIDTH/2 - 20, 20, timeSurf->w, timeSurf->h};
                SDL_RenderCopy(renderer,timeTex,nullptr,&tr);
                SDL_DestroyTexture(timeTex); SDL_FreeSurface(timeSurf);
            }

            if(state==GameState::Result){
                std::ostringstream resultText;
                if(playerOneScore>playerTwoScore) resultText<<"PLAYER gana!";
                else if(playerTwoScore>playerOneScore) resultText<<"CPU gana!";
                else resultText<<"Empate!";
                resultText<<" SPACE para menu";

                SDL_Surface* textSurf = TTF_RenderText_Solid(scoreFont,resultText.str().c_str(),{255,255,255,255});
                SDL_Texture* textTex = SDL_CreateTextureFromSurface(renderer,textSurf);
                SDL_Rect r{WINDOW_WIDTH/4, WINDOW_HEIGHT/2 - 20, textSurf->w, textSurf->h};
                SDL_RenderCopy(renderer,textTex,nullptr,&r);
                SDL_DestroyTexture(textTex); SDL_FreeSurface(textSurf);
            }
        }
//---7)Presentar frame
//Muestra todo lo que se dibujó en la ventana.
        SDL_RenderPresent(renderer);

//---8)Calcular delta time
//dt se usa para que los movimientos dependan del tiempo real, no de la velocidad del computador.
//Esto evita que el juego sea más rápido en PCs potentes.
        auto frameEnd = std::chrono::high_resolution_clock::now();
        dt = std::chrono::duration<float,std::chrono::milliseconds::period>(frameEnd-frameStart).count();
    }


    //6)Liberar recursos y cerrar SDL
    SDL_DestroyTexture(paddlePlayerTex); SDL_DestroyTexture(paddleCPUTex); SDL_DestroyTexture(ballTex); 
    SDL_DestroyTexture(menuBackground); SDL_DestroyTexture(gameplayBackground);
    Mix_FreeChunk(wallHitSound); Mix_FreeChunk(paddleHitSound);
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    TTF_CloseFont(scoreFont); Mix_Quit(); TTF_Quit(); IMG_Quit(); SDL_Quit();
    return 0;
}
//Que funcione D: