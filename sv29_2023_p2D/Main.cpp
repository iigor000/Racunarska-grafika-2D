#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <numbers>
#include "Util.h"
#include <string>

int passengerCount = 0;
static float velocity = 0.0f;
int sickPassengerIndex = -1;
bool seatbeltOn[8] = { false };
bool isPassengerGreen[8] = { false };
bool passengerPresent[8] = { false };
double sickTimer = 0.0f; // Timer za 10 sekundi mirovanja

GLFWcursor* cursor;
GLFWcursor* cursorPressed;

// Vrednosti za prugu
const float trackLen = 0.95f;
const float straghtTrackPart = 0.35f;
const int numTrackVertices = 200;
const float returnTrackY = -0.3f;
const float brakeStartX = trackLen - straghtTrackPart;
const float returnSpeed = 0.002f;
const float sickCrawlSpeed = 0.001f;

// Vrednosti za vozilo
const float startingVehiclePosition = -0.6f;
const float baseCartDistance = 0.042f;

float headX = startingVehiclePosition;

// Stanje vagona
enum CartPhase {
    CART_PARKED = 0,
    CART_ON_TRACK,
    CART_RETURNING,
    CART_TO_START
};
CartPhase cartPhase[8];
float cartX[8];

// Stanja simulacije
enum RollercoasterState {
    WAITING_FOR_BOARDING,
    RUNNING,
    SICK_STOPPING,
    SICK_WAITING,
    SICK_CRAWLING,
    WAITING_FOR_UNBOARDING
};

RollercoasterState currentState = WAITING_FOR_BOARDING;

// Stanje oblaka
struct Cloud {
    float x, y;
    float speed;
    float width, height;
    int textureIndex;
};

// Niz oblaka
const int MAX_CLOUDS = 6;
Cloud clouds[MAX_CLOUDS];

// Niz za teksture oblaka i VAO/VBO
unsigned int cloudTextures[3];
unsigned int cloudVAO;

// Lokacije uniformi
GLint uOffsetLoc = -1;
GLint uRotationLoc = -1;
GLint uScaleLoc = -1;
GLint uUseTextureLoc = -1;
GLint uUseTex1Loc = -1;
GLint uUseTex2Loc = -1;

// Staticki VAO/VBO koji se prave jednom
unsigned int supportVAO = 0, supportVBO = 0;
int supportVertexCount = 0;
unsigned int returnTrackVAO = 0, returnTrackVBO = 0;

int endProgram(std::string message) {
    std::cout << message << std::endl;
    glfwTerminate();
    return -1;
}

// Vraca Y za prugu na osnovu X
float getTrackY(float x, float trackLen, float straightPart) {
    float calculatePart = trackLen - straightPart;
    if (x < -calculatePart || x > calculatePart) {
        return -0.3f;
    }
    float t = (x + calculatePart) / (calculatePart * 2.0f);
    float envelope = sin(t * std::numbers::pi);
    float baseWave = sin((x * (3.0f / calculatePart) * std::numbers::pi) + std::numbers::pi / 2.0f);
    float variation = 0.3f * sin(x * 12.0f) + 0.3f;
    return ((baseWave + variation) / 5.0f + 0.2f) * envelope - 0.3f;
}

void generateTrack(int numPoints, float vertices[], int step, float trackLen, float straightPart) {
    for (int i = 0; i < numPoints; ++i) {
        float x = -trackLen + (trackLen * 2 * i) / (numPoints - 1);
        float y;

        y = getTrackY(x, trackLen, straightPart);

        vertices[i * step] = x;
        vertices[i * step + 1] = y;

        vertices[i * step + 2] = 0.28f;
        vertices[i * step + 3] = 0.16f;
        vertices[i * step + 4] = 0.08f;
        vertices[i * step + 5] = 1.0f;

        vertices[i * step + 6] = 0.0f;
        vertices[i * step + 7] = 0.0f;
    }
}

// Dobijamo ugao rotacije u tacki X (radijani)
float getTrackAngle(float x) {
    float delta = 0.005f; // Mala promena X za racunanje nagiba
    float y1 = getTrackY(x - delta, trackLen, straghtTrackPart);
    float y2 = getTrackY(x + delta, trackLen, straghtTrackPart);
    float dy = y2 - y1;
    float dx = 2.0f * delta;
    return atan2(dy, dx); // atan2 daje ugao tangente na krivi
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {

    // Zatvaranje prozora na esc
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // Kad pritisnemo space, ako je voz u vazi boardovanja, dodajemo putnike
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS && currentState == WAITING_FOR_BOARDING) {
        if (passengerCount < 8) {
            passengerPresent[passengerCount] = true;
            passengerCount++;
        }
    }

    // Kad pritisnemo etner, ako voz nije krenuo, ima putnika i svi su vezani, on krece
    if (key == GLFW_KEY_ENTER && action == GLFW_PRESS && currentState == WAITING_FOR_BOARDING) {
        if (passengerCount == 0) {
            return;
        }

        // Proveravamo da li su svi ukrcani putnici vezani
        bool allFastened = true;
        for (int i = 0; i < passengerCount; ++i) {
            if (!seatbeltOn[i]) {
                allFastened = false;
                break;
            }
        }

        if (allFastened) {
            currentState = RUNNING;
            for (int i = 0; i < 8; ++i) {
                cartPhase[i] = CART_ON_TRACK;
            }
        }
    }

    // Pracenje tastera 1-8, tokom voznje (bolesni putnici)
    if (currentState == RUNNING && cartPhase[0] == CART_ON_TRACK && action == GLFW_PRESS) {
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_8) {

            // Pretvaramo taster u broj 0-7
            int pressedIndex = key - GLFW_KEY_1;

            // Radi samo ako je mesto zauzeto
            if (pressedIndex < passengerCount) {
                sickPassengerIndex = pressedIndex;
                isPassengerGreen[sickPassengerIndex] = true;
                currentState = SICK_STOPPING;
            }
        }
    }
}

void mouseCallback(GLFWwindow* window, int button, int action, int mods) {

    // Levi klik misa (sluzi za vezivanje pojaseva i iskrcavanje putnika)
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        glfwSetCursor(window, cursorPressed);

        // Ako vozilo vozi ili stoji zbog mučnine, klikovi se ignorišu
        if (currentState == RUNNING || currentState == SICK_STOPPING ||
            currentState == SICK_WAITING || currentState == SICK_CRAWLING) {
            return;
        }

        // Prilagodjavama koordinate nasem viewport-u
        // Ista logika kao u main petlji
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        int viewportWidth = width;
        int viewportHeight = width;
        int viewportX = 0;
        int viewportY = (height - viewportHeight) / 2;

        // Racunamo realne vrednosti
        float glX = ((float)(mouseX - viewportX) / (float)viewportWidth) * 2.0f - 1.0f;
        float glY = 1.0f - ((float)(mouseY - viewportY) / (float)viewportHeight) * 2.0f;

        // Proveravamo klik na svakog prisutnog putnika
        for (int i = 0; i < passengerCount; ++i) {
            float carX = startingVehiclePosition - (i * baseCartDistance);
            float carY = getTrackY(carX, trackLen, straghtTrackPart);

            float clickRadius = 0.035f;

            if (abs(glX - carX) <= clickRadius && abs(glY - carY) <= clickRadius) {

                // 1. Stanje vezivanja pojaseva pre vožnje
                if (currentState == WAITING_FOR_BOARDING) {
                    seatbeltOn[i] = true;
                    break;
                }

                // 2. Stanje iskrcavanja (nema pomeranja, samo gasi prisustvo putnika na tom mestu ako je tu sedeo)
                else if (currentState == WAITING_FOR_UNBOARDING) {
                    if (passengerPresent[i]) {
                        passengerPresent[i] = false; // Putnik je izvađen sa ovog mesta, vagon ostaje prazan

                        // Proveravamo da li su svi putnici izvađeni iz svojih sedišta
                        bool allEmpty = true;
                        for (int p = 0; p < passengerCount; ++p) {
                            if (passengerPresent[p]) {
                                allEmpty = false;
                                break;
                            }
                        }

                        // Kada su svi uklonjeni, resetujemo sve i dozvoljavamo novo ukrcavanje
                        if (allEmpty) {
                            passengerCount = 0;
                            for (int p = 0; p < 8; ++p) {
                                passengerPresent[p] = false;
                                seatbeltOn[p] = false;
                                isPassengerGreen[p] = false;
                            }
                            currentState = WAITING_FOR_BOARDING;
                        }
                    }
                    break;
                }
            }
        }
    }

    // Postavljanje kurosra na kliknut
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        glfwSetCursor(window, cursor);
    }
}

void preprocessTextureRepeat(unsigned& texture, const char* filepath) {
    texture = loadImageToTexture(filepath); // Učitavanje teksture
    glBindTexture(GL_TEXTURE_2D, texture); // Vezujemo se za teksturu kako bismo je podesili

    // Podešavanje strategija za wrap-ovanje - šta da radi kada se dimenzije teksture i poligona ne poklapaju
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // S - tekseli po x-osi
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); // T - tekseli po y-osi

    // Podešavanje algoritma za smanjivanje i povećavanje rezolucije: nearest - bira najbliži piksel
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void preprocessTexture(unsigned& texture, const char* filepath) {
    texture = loadImageToTexture(filepath); // Učitavanje teksture
    glBindTexture(GL_TEXTURE_2D, texture); // Vezujemo se za teksturu kako bismo je podesili

    // Podešavanje strategija za wrap-ovanje - nema wrap-a
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Podešavanje algoritma za smanjivanje i povećavanje rezolucije: nearest - bira najbliži piksel
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void formVAO(float* vertices, size_t size, unsigned int& VAO, unsigned int& VBO) {
    // formiranje VAO-ova je izdvojeno u posebnu funkciju radi čitljivijeg koda u main funkciji
    // VBO se vraca pozivaocu da bi mogao da se obrise kada VAO vise nije potreban
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);

    // Atribut 0 (pozicija):
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Atribut 1 (boja):
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Atribut 2 (tekseli):
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

void setViewport(GLFWwindow* window) {
    // Postavljane viewporta
    // Cilj je da napravimo "zoom and crop", tako da ekran ostane u dimenzijama 1:1,
    // tako da su i visina i sirina viewporta jednaki sirini ekrana (kako ne bi doslo do stretchovanja tekstura),
    // Za rolerkoster je bitno da bue po sirini, jer inace ne stane lepo na ekran
    int width, height;
    glfwGetFramebufferSize(window, &width, &height); // Citamo trenutnu velicinu ekrana

    // Sirina viewport-a je sirina prozora
    int viewportWidth = width;
    // Visina je ista kao sirina, kako bi ostao 1:1 prozor
    int viewportHeight = width;

    int viewportX = 0;
    int viewportY = (height - viewportHeight) / 2;

    // Postavljamo izracunati viewport
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
}

// Postavlja transformaciju koju basic.vert primenjuje na sva temena (skaliranje -> rotacija -> pomeraj)
void setTransform(float offsetX, float offsetY, float rotation, float scaleX, float scaleY) {
    glUniform2f(uOffsetLoc, offsetX, offsetY);
    glUniform1f(uRotationLoc, rotation);
    glUniform2f(uScaleLoc, scaleX, scaleY);
}

// Vraca transformaciju na identitet (da ostali objekti koji crtaju direktno u NDC ostanu netaknuti)
void resetTransform() {
    setTransform(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
}

void drawTrack(unsigned int shader, unsigned int trackVAO, int numTrackVertices) {
    // Koristimo shader
    glUseProgram(shader);

    glUniform1i(uUseTextureLoc, 0);

    // Crtanje staze
    glBindVertexArray(trackVAO);
    glLineWidth(5.0f);
    glDrawArrays(GL_LINE_STRIP, 0, numTrackVertices);
}

void drawStudentInfo(unsigned int shader, unsigned int studentVAO, unsigned int studentTexture) {
    glUseProgram(shader);

    glUniform1i(uUseTextureLoc, 1);

    glBindTexture(GL_TEXTURE_2D, studentTexture);
    glBindVertexArray(studentVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void drawGrass(unsigned int shader, unsigned int grassVAO, unsigned int grassTexture) {
    glUseProgram(shader);

    glUniform1i(uUseTextureLoc, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, grassTexture);

    glBindVertexArray(grassVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

// Stubovi su staticki, pa se geometrija pravi samo jednom pri pokretanju
void buildSupports(const float trackVertices[], int step) {
    const int maxSupportVertices = (numTrackVertices / 5 + 1) * 2 * 8;
    float supportVertices[maxSupportVertices];
    int vertexCount = 0;

    for (int i = 0; i < numTrackVertices; i += 5) {
        float x = trackVertices[i * step];
        float y = trackVertices[i * step + 1];

        // Stubove crtamo samo tamo gde je pruga iznad tla
        if (y > -0.29f) {
            float groundY = -0.30f; // Visina tla do koje stub ide

            // 1. Teme na pruzi (vrh stuba) - siva boja
            supportVertices[vertexCount++] = x;
            supportVertices[vertexCount++] = y;
            supportVertices[vertexCount++] = 0.5f;
            supportVertices[vertexCount++] = 0.5f;
            supportVertices[vertexCount++] = 0.5f;
            supportVertices[vertexCount++] = 1.0f;
            supportVertices[vertexCount++] = 0.0f;
            supportVertices[vertexCount++] = 0.0f;

            // 2. Teme na tlu (dno stuba) - tamnija siva boja
            supportVertices[vertexCount++] = x;
            supportVertices[vertexCount++] = groundY;
            supportVertices[vertexCount++] = 0.3f;
            supportVertices[vertexCount++] = 0.3f;
            supportVertices[vertexCount++] = 0.3f;
            supportVertices[vertexCount++] = 1.0f;
            supportVertices[vertexCount++] = 0.0f;
            supportVertices[vertexCount++] = 0.0f;
        }
    }

    supportVertexCount = vertexCount / 8;
    if (vertexCount == 0) return;

    formVAO(supportVertices, vertexCount * sizeof(float), supportVAO, supportVBO);
}

void drawSupports(unsigned int shader) {
    if (supportVertexCount == 0) return;

    glUseProgram(shader);
    glUniform1i(uUseTextureLoc, 0);

    glBindVertexArray(supportVAO);
    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, supportVertexCount);
}

void buildReturnTrack() {
    // Linija ide od kraja gornje staze do pocetne pozicije vozila
    float returnVertices[] = {
        //       x       y              r      g     b      a      s     t
             trackLen, returnTrackY,  0.28f, 0.16f, 0.08f, 1.0f,  0.0f, 0.0f,
            -trackLen, returnTrackY,  0.28f, 0.16f, 0.08f, 1.0f,  0.0f, 0.0f
    };
    formVAO(returnVertices, sizeof(returnVertices), returnTrackVAO, returnTrackVBO);
}

void drawReturnTrack(unsigned int shader) {
    glUseProgram(shader);
    glUniform1i(uUseTextureLoc, 0);

    glBindVertexArray(returnTrackVAO);
    glLineWidth(5.0f);
    glDrawArrays(GL_LINES, 0, 2);
}

// Da li je vagon okrenut nazad (1) ili gleda napred (0)
float getCartFlip(int i) {
    return (cartPhase[i] == CART_RETURNING) ? 1.0f : 0.0f;
}

float getParkX(int i) {
    return startingVehiclePosition - i * baseCartDistance;
}

// Za svaki vagon: voznja -> (okret) povratak do levog kraja -> (okret) udesno do starta -> parkiranje
void updateCarts() {
    float chainX = headX; // Pozicija lanca vagona iza glave vozila

    float backSpeed = (sickPassengerIndex >= 0) ? sickCrawlSpeed : returnSpeed;

    for (int i = 0; i < 8; ++i) {
        // Priblizavamo ih na uzbrdici/nizbrdici
        if (i > 0) {
            float slope = tan(getTrackAngle(chainX));
            chainX -= baseCartDistance / sqrt(1.0f + slope * slope);
        }

        switch (cartPhase[i]) {
        case CART_PARKED:
            cartX[i] = getParkX(i);
            break;

        case CART_ON_TRACK:
            cartX[i] = chainX;
            if (chainX >= trackLen) {
                // Stigao do desnog kraja - okrece se
                cartX[i] = trackLen;
                cartPhase[i] = CART_RETURNING;
            }
            break;

        case CART_RETURNING:
            cartX[i] -= backSpeed;
            if (cartX[i] <= -trackLen) {
                // Stigao do levog kraja pruge - okrece se
                cartX[i] = -trackLen;
                cartPhase[i] = CART_TO_START;
            }
            break;

        case CART_TO_START:
            cartX[i] += backSpeed;
            if (cartX[i] >= getParkX(i)) {
                cartX[i] = getParkX(i);
                cartPhase[i] = CART_PARKED;
            }
            break;
        }
    }
}

void finishRide() {
    headX = startingVehiclePosition;
    velocity = 0.0f;
    currentState = WAITING_FOR_UNBOARDING;
    sickPassengerIndex = -1;

    // Automatski odvezujemo sve pojaseve i čistimo zelenilo
    for (int b = 0; b < 8; ++b) {
        seatbeltOn[b] = false;
        isPassengerGreen[b] = false;
    }
}

void updatePhysics() {
    bool rideActive = true;

    // Stanje dok se krece vozilo
    if (currentState == RUNNING) {
        float currentAngle = getTrackAngle(headX);
        float slope = tan(currentAngle);

        if (headX > brakeStartX) {
            // Na kraju staze kocimo
            velocity *= 0.95f;
            if (velocity < returnSpeed) velocity = returnSpeed;
        }
        else {
            float gravityEffect = -slope * 0.00015f;
            velocity += gravityEffect;

            float minSpeed = 0.001f;
            if (velocity < minSpeed) velocity = minSpeed;
        }

        float arcCorrection = sqrt(1.0f + slope * slope);
        headX += velocity / arcCorrection;
    }
    // Ako treba da se stane zbog mucnine putnika
    else if (currentState == SICK_STOPPING) {
        velocity *= 0.95f;
        float arcCorrection = sqrt(1.0f + pow(tan(getTrackAngle(headX)), 2));
        headX += velocity / arcCorrection;

        if (velocity < 0.0001f) {
            velocity = 0.0f;
            currentState = SICK_WAITING;
            sickTimer = glfwGetTime();
        }
    }
    // Ako smo vec stali zbog mucnine putnika
    else if (currentState == SICK_WAITING) {
        if (glfwGetTime() - sickTimer >= 10.0) {
            currentState = SICK_CRAWLING;
        }
    }
    // Nakon cekanja zbog mucnine ide polako napred konstantnom brzinom
    else if (currentState == SICK_CRAWLING) {
        velocity = sickCrawlSpeed;
        float arcCorrection = sqrt(1.0f + pow(tan(getTrackAngle(headX)), 2));
        headX += velocity / arcCorrection;
    }
    else {
        // WAITING_FOR_BOARDING ili WAITING_FOR_UNBOARDING
        rideActive = false;
        headX = startingVehiclePosition;
        velocity = 0.0f;
    }

    updateCarts();

    // Kad svi vagoni budu parkirano gotova je voznja
    if (rideActive) {
        bool allParked = true;
        for (int i = 0; i < 8; ++i) {
            if (cartPhase[i] != CART_PARKED) {
                allParked = false;
                break;
            }
        }
        if (allParked) finishRide();
    }
}

// Crta sve vagone
void drawCarts(unsigned int shader, unsigned int cartVAO,
    unsigned int unitCartTexture, unsigned int personTextures[],
    unsigned int personGreenTextures[], unsigned int seatbeltTexture) {

    glUseProgram(shader);
    glUniform1i(uUseTextureLoc, 1);
    glBindVertexArray(cartVAO);

    // Teksture koje se ne menjaju od vagona do vagona vezujemo samo jednom
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, unitCartTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, seatbeltTexture);
    glActiveTexture(GL_TEXTURE1); // ostaje aktivna jer se tekstura putnika menja po vagonu

    for (int i = 0; i < 8; ++i) {
        float carX = cartX[i];
        if (carX < -1.0f || carX > 1.0f) continue;

        // flip: 0 = gleda napred (glavna pruga), 1 = gleda nazad
        float flip = getCartFlip(i);
        float scaleX = (flip > 0.5f) ? -1.0f : 1.0f;

        float trackY = getTrackY(carX, trackLen, straghtTrackPart);
        float carY = trackY + (returnTrackY - trackY) * flip;
        float angle = getTrackAngle(carX) * (1.0f - flip);

        float cosA = cos(angle);
        float sinA = sin(angle);

        float heightOffset = 0.02f;
        float finalX = carX - heightOffset * sinA;
        float finalY = carY + heightOffset * cosA;

        // Pomeraj, rotacija i flip (scaleX) se racunaju u vertex shaderu
        setTransform(finalX, finalY, angle, scaleX, 1.0f);

        bool hasPerson = (i < passengerCount && passengerPresent[i]);
        bool hasSeatbelt = hasPerson && seatbeltOn[i];

        // Putnik (zdrav ili zeleni/bolesni) ide na teksturnu jedinicu 1
        if (hasPerson) {
            glBindTexture(GL_TEXTURE_2D, isPassengerGreen[i] ? personGreenTextures[i] : personTextures[i]);
        }
        glUniform1i(uUseTex1Loc, hasPerson);
        glUniform1i(uUseTex2Loc, hasSeatbelt);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // Vracamo stanje da ostali objekti ostanu netaknuti
    glUniform1i(uUseTex1Loc, 0);
    glUniform1i(uUseTex2Loc, 0);
    glActiveTexture(GL_TEXTURE0);
    resetTransform();
}

void drawClouds(unsigned int shader, unsigned int cloudVAO) {
    glUseProgram(shader);
    glUniform1i(uUseTextureLoc, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(cloudVAO);

    for (int i = 0; i < MAX_CLOUDS; ++i) {
        // Krece se ka napred
        clouds[i].x += clouds[i].speed;

        // Ako oblak predje desnu ivicu ekrana, vraćamo ga skroz levo da ide ponovo
        if (clouds[i].x > 1.3f) {
            clouds[i].x = -1.3f;
            // Dobija novu random visinu
            clouds[i].y = 0.25f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f;
        }

        setTransform(clouds[i].x, clouds[i].y, 0.0f, clouds[i].width, clouds[i].height);

        glBindTexture(GL_TEXTURE_2D, cloudTextures[clouds[i].textureIndex]);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    resetTransform();
}

int main()
{
    // Inicijalizacija GLFW i postavljanje na verziju 3 sa programabilnim pajplajnom
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Uzimanje primarnog monitora za fullscreen režim
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

    // Formiranje prozora za prikaz sa datim dimenzijama i naslovom
    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "Rolerkoster - SV29/2023", primaryMonitor, NULL);
    if (window == NULL) return endProgram("Prozor nije uspeo da se kreira.");
    glfwMakeContextCurrent(window);

    // Inicijalizacija GLEW
    if (glewInit() != GLEW_OK) return endProgram("GLEW nije uspeo da se inicijalizuje.");

    unsigned int basicShader = createShader("basic.vert", "basic.frag");
    glUseProgram(basicShader);
    glUniform1i(glGetUniformLocation(basicShader, "uTex0"), 0);
    glUniform1i(glGetUniformLocation(basicShader, "uTex1"), 1);
    glUniform1i(glGetUniformLocation(basicShader, "uTex2"), 2);

    // Lokacije uniformi
    uOffsetLoc = glGetUniformLocation(basicShader, "uOffset");
    uRotationLoc = glGetUniformLocation(basicShader, "uRotation");
    uScaleLoc = glGetUniformLocation(basicShader, "uScale");
    uUseTextureLoc = glGetUniformLocation(basicShader, "uUseTexture");
    uUseTex1Loc = glGetUniformLocation(basicShader, "uUseTex1");
    uUseTex2Loc = glGetUniformLocation(basicShader, "uUseTex2");
    resetTransform();

    // Callback za mis i taster
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseCallback);

    // Generisanje pruge
    float trackVertices[numTrackVertices * 8];
    generateTrack(numTrackVertices, trackVertices, 8, trackLen, straghtTrackPart);
    unsigned int trackVAO, trackVBO;
    formVAO(trackVertices, sizeof(trackVertices), trackVAO, trackVBO);

    // Stubovi i povratna linija su staticki - pravimo ih jednom
    buildSupports(trackVertices, 8);
    buildReturnTrack();

    // Informacije o meni
    float studentInfoSquare[] = {
        //   x      y       r     g     b     a      s     t
            0.3f,  0.58f, 1.0f, 1.0f, 1.0f, 1.0f,     0.0f, 1.0f,  // gornje levo
            0.3f,  0.48f, 1.0f, 1.0f, 1.0f, 1.0f,     0.0f, 0.0f,  // donje levo
            1.0f,  0.48f, 1.0f, 1.0f, 1.0f, 1.0f,     1.0f, 0.0f,  // donje desno
            1.0f,  0.58f, 1.0f, 1.0f, 1.0f, 1.0f,     1.0f, 1.0f   // gornje desno
    };
    unsigned int studentVAO, studentVBO;
    formVAO(studentInfoSquare, sizeof(studentInfoSquare), studentVAO, studentVBO);
    unsigned int studentTexture;
    preprocessTexture(studentTexture, "res/watermark.png");

    // Jedan vagon vozila (sluzi za teksele i velicinu, lokacija se posle menja)
    float unitCarSquare[] = {
        //   x       y       r     g     b     a         s     t
          -0.020f,  0.020f, 1.0f, 1.0f, 1.0f, 1.0f,    0.0f, 1.0f, // gornje levo
          -0.020f, -0.020f, 1.0f, 1.0f, 1.0f, 1.0f,    0.0f, 0.0f, // donje levo
           0.020f, -0.020f, 1.0f, 1.0f, 1.0f, 1.0f,    1.0f, 0.0f, // donje desno
           0.020f,  0.020f, 1.0f, 1.0f, 1.0f, 1.0f,    1.0f, 1.0f  // gornje desno
    };
    unsigned int cartVAO, cartVBO;
    formVAO(unitCarSquare, sizeof(unitCarSquare), cartVAO, cartVBO);
    unsigned int unitCartTexture;
    preprocessTexture(unitCartTexture, "res/cart.png");

    // Teksture za ljude i pojas
    unsigned int personTextures[8];
    unsigned int personGreenTextures[8];
    // Ucitavanje tekstura za osobe
    for (int i = 0; i < 8; ++i) {
        // Formiramo putanju npr. "res/human1.png", "res/human2.png"...
        std::string personPath = "res/human" + std::to_string(i + 1) + ".png";
        std::string greenPath = "res/human" + std::to_string(i + 1) + "-green.png";

        preprocessTexture(personTextures[i], personPath.c_str());
        preprocessTexture(personGreenTextures[i], greenPath.c_str());
    }

    unsigned int seatbeltTexture;
    preprocessTexture(seatbeltTexture, "res/seatbelt.png");

    // Trava na dnu ekrana
    float grassSquare[] = {
        //   x       y       r     g     b     a         s     t
           -1.0f, -0.3f,   1.0f, 1.0f, 1.0f, 1.0f,     0.0f, 8.0f, // gornje levo
           -1.0f, -1.0f,   1.0f, 1.0f, 1.0f, 1.0f,     0.0f, 0.0f, // donje levo
            1.0f, -1.0f,   1.0f, 1.0f, 1.0f, 1.0f,     32.0f, 0.0f, // donje desno
            1.0f, -0.3f,   1.0f, 1.0f, 1.0f, 1.0f,     32.0f, 8.0f  // gornje desno
    };

    unsigned int grassVAO, grassVBO;
    formVAO(grassSquare, sizeof(grassSquare), grassVAO, grassVBO);
    unsigned int grassTexture;
    preprocessTextureRepeat(grassTexture, "res/grass.png");

    // Ucitavanje kursora
    cursor = loadImageToCursor("res/cursor.png");
    cursorPressed = loadImageToCursor("res/cursor_clicked.png");
    glfwSetCursor(window, cursor);

    // Učitavanje tekstura oblaka
    preprocessTexture(cloudTextures[0], "res/cloud1.png");
    preprocessTexture(cloudTextures[1], "res/cloud2.png");
    preprocessTexture(cloudTextures[2], "res/cloud3.png");

    // Kreiramo jedan generalni kvadrat za oblake
    float cloudSquare[] = {
        //   x       y       r     g     b     a         s     t
          -0.5f,  0.5f,  1.0f, 1.0f, 1.0f, 1.0f,    0.0f, 1.0f, // gornje levo
          -0.5f, -0.5f,  1.0f, 1.0f, 1.0f, 1.0f,    0.0f, 0.0f, // donje levo
           0.5f, -0.5f,  1.0f, 1.0f, 1.0f, 1.0f,    1.0f, 0.0f, // donje desno
           0.5f,  0.5f,  1.0f, 1.0f, 1.0f, 1.0f,    1.0f, 1.0f  // gornje desno
    };
    unsigned int cloudVBO;
    formVAO(cloudSquare, sizeof(cloudSquare), cloudVAO, cloudVBO);

    // Pravimo oblake
    for (int i = 0; i < MAX_CLOUDS; ++i) {
        clouds[i].x = -1.2f + (static_cast<float>(rand()) / RAND_MAX) * 2.4f;
        clouds[i].y = 0.25f + (static_cast<float>(rand()) / RAND_MAX) * 0.4f; 
        clouds[i].speed = 0.0003f + (static_cast<float>(rand()) / RAND_MAX) * 0.0005f; 
        clouds[i].textureIndex = i % 3;

        // Velicina na osovu dimenzija slika
        if (clouds[i].textureIndex == 0) {
            clouds[i].width = 0.25f; clouds[i].height = 0.125f; // cloud1 (64x32)
        }
        else if (clouds[i].textureIndex == 1) {
            clouds[i].width = 0.3f;  clouds[i].height = 0.15f;  // cloud2 (80x40)
        }
        else {
            clouds[i].width = 0.2f;  clouds[i].height = 0.2f;   // cloud3 (40x40)
        }
    }

    // Postavljanje boje pozadine (svetlo plava)
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f);

    // Postavljamo vrednosti za fps limiter
    const double TARGET_FPS = 75.0;
    const double TARGET_FRAME_TIME = 1.0 / TARGET_FPS;
    double lastFrameTime = glfwGetTime();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (!glfwWindowShouldClose(window))
    {
        // Postavljamo viewport
        setViewport(window);

        // Logika za fps limiter - gledamo koliko je proslo od prethodnog frejma, ako je manje 
        // od pozeljnog, cekamo da prodje to odredjeno vreme
        double currentTime = glfwGetTime();
        double deltaTime = currentTime - lastFrameTime;

        if (deltaTime < TARGET_FRAME_TIME) {
            double sleepTime = TARGET_FRAME_TIME - deltaTime;
            glfwWaitEventsTimeout(sleepTime);
            continue;
        }
        lastFrameTime = glfwGetTime();

        // Bojimo pozadinu
        glClear(GL_COLOR_BUFFER_BIT);

        // Crtanje trave
        drawGrass(basicShader, grassVAO, grassTexture);

        // Crtanje oblaka
        drawClouds(basicShader, cloudVAO);

        // Crtanje suportova
        drawSupports(basicShader);

        // Crtanje staze
        drawTrack(basicShader, trackVAO, numTrackVertices);

        // Crtanje povratnog dela staze
        drawReturnTrack(basicShader);

        // Fizika za kretanje vagona
        updatePhysics();

        // Crtanje vagona (pod nagibom)
        drawCarts(basicShader, cartVAO, unitCartTexture, personTextures, personGreenTextures, seatbeltTexture);

        // Crtanje indexa
        drawStudentInfo(basicShader, studentVAO, studentTexture);

        glfwSwapBuffers(window); // Zamena bafera - prednji i zadnji bafer se menjaju kao štafeta; dok jedan procesuje, drugi se prikazuje.
        glfwPollEvents(); // Sinhronizacija pristiglih događaja
    }

    // Oslobadjanje GPU resursa
    unsigned int vaos[] = { trackVAO, studentVAO, cartVAO, grassVAO, cloudVAO, supportVAO, returnTrackVAO };
    unsigned int vbos[] = { trackVBO, studentVBO, cartVBO, grassVBO, cloudVBO, supportVBO, returnTrackVBO };
    glDeleteVertexArrays(7, vaos);
    glDeleteBuffers(7, vbos);

    glDeleteTextures(1, &studentTexture);
    glDeleteTextures(1, &unitCartTexture);
    glDeleteTextures(1, &seatbeltTexture);
    glDeleteTextures(1, &grassTexture);
    glDeleteTextures(8, personTextures);
    glDeleteTextures(8, personGreenTextures);
    glDeleteTextures(3, cloudTextures);
    glDeleteProgram(basicShader);

    glfwTerminate();
    return 0;
}