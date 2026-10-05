#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr float WindowWidth = 800.f;
    constexpr float WindowHeight = 600.f;
    constexpr float TileSize = 40.f;

    constexpr float PlayerWidth = 32.f;
    constexpr float PlayerHeight = 48.f;

    constexpr float MoveSpeed = 260.f;
    constexpr float Gravity = 1400.f;
    constexpr float JumpSpeed = 600.f;
    constexpr float MaximumFallSpeed = 900.f;
    constexpr float FixedStep = 1.f / 120.f;

    constexpr int MaximumScore = 800;

    enum class State
    {
        Menu,
        Playing,
        Paused,
        Won,
        GameOver
    };

    enum class Cue
    {
        Jump,
        Coin,
        Stomp,
        Damage,
        Checkpoint,
        Win
    };

    class Audio
    {
    public:
        Audio()
        {
            const std::array<float, 6> start{
                250.f, 900.f, 180.f, 220.f, 500.f, 600.f
            };

            const std::array<float, 6> finish{
                650.f, 1400.f, 70.f, 50.f, 1100.f, 1500.f
            };

            const std::array<float, 6> duration{
                0.12f, 0.12f, 0.15f, 0.25f, 0.30f, 0.55f
            };

            constexpr unsigned int SampleRate = 44100;
            constexpr float Pi = 3.14159265359f;

            for (std::size_t sound = 0; sound < buffers.size(); ++sound)
            {
                const auto count = static_cast<std::size_t>(
                    duration[sound] * SampleRate);

                std::vector<std::int16_t> samples(count);
                float phase = 0.f;

                for (std::size_t i = 0; i < count; ++i)
                {
                    const float progress =
                        static_cast<float>(i)
                        / static_cast<float>(count);

                    const float frequency =
                        start[sound]
                        + (finish[sound] - start[sound]) * progress;

                    phase += 2.f * Pi * frequency / SampleRate;

                    const float attack =
                        std::min(1.f, progress / 0.03f);

                    const float decay =
                        (1.f - progress) * (1.f - progress);

                    const float sample =
                        std::sin(phase) * attack * decay * 0.4f;

                    samples[i] = static_cast<std::int16_t>(
                        sample * 32767.f);
                }

                ready[sound] = buffers[sound].loadFromSamples(
                    samples.data(),
                    samples.size(),
                    1,
                    SampleRate,
                    { sf::SoundChannel::Mono });
            }
        }

        Audio(const Audio&) = delete;
        Audio& operator=(const Audio&) = delete;

        void play(Cue cue)
        {
            const auto index = static_cast<std::size_t>(cue);

            if (muted || paused || !ready[index])
                return;

            cleanup();

            if (voices.size() >= 16)
            {
                voices.front()->stop();
                voices.erase(voices.begin());
            }

            auto voice =
                std::make_unique<sf::Sound>(buffers[index]);

            voice->setVolume(40.f);
            voice->play();

            voices.push_back(std::move(voice));
        }

        void cleanup()
        {
            voices.erase(
                std::remove_if(
                    voices.begin(),
                    voices.end(),
                    [](const std::unique_ptr<sf::Sound>& voice)
                    {
                        return voice->getStatus()
                            == sf::SoundSource::Status::Stopped;
                    }),
                voices.end());
        }

        void setPaused(bool value)
        {
            if (paused == value)
                return;

            paused = value;

            for (auto& voice : voices)
            {
                if (paused
                    && voice->getStatus()
                    == sf::SoundSource::Status::Playing)
                {
                    voice->pause();
                }
                else if (!paused
                    && voice->getStatus()
                    == sf::SoundSource::Status::Paused)
                {
                    voice->play();
                }
            }
        }

        void toggleMute()
        {
            muted = !muted;

            if (muted)
                clear();
        }

        bool isMuted() const
        {
            return muted;
        }

        void clear()
        {
            for (auto& voice : voices)
                voice->stop();

            voices.clear();
        }

    private:
        // Voices are destroyed before the buffers they use.
        std::array<sf::SoundBuffer, 6> buffers;
        std::array<bool, 6> ready{};

        std::vector<std::unique_ptr<sf::Sound>> voices;

        bool muted = false;
        bool paused = false;
    };

    std::filesystem::path highScorePath()
    {
        std::filesystem::path base;

#ifdef _WIN32
        char* folder = nullptr;
        std::size_t length = 0;

        if (_dupenv_s(&folder, &length, "LOCALAPPDATA") == 0
            && folder != nullptr)
        {
            base = folder;
        }

        std::free(folder);
#endif

        if (base.empty())
        {
            std::error_code error;
            base = std::filesystem::current_path(error);

            if (error)
                base = ".";
        }

        return base / "VBTutor" / "PlatformAdventure"
            / "highscore.txt";
    }

    int loadHighScore(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        int score = 0;

        if (input >> score)
        {
            if (score >= 0 && score <= MaximumScore)
                return score;
        }

        return 0;
    }

    bool saveHighScore(
        const std::filesystem::path& path,
        int score)
    {
        std::error_code error;

        std::filesystem::create_directories(
            path.parent_path(), error);

        if (error)
            return false;

        std::ofstream output(path, std::ios::trunc);

        if (!output)
            return false;

        output << score << '\n';
        output.flush();

        return static_cast<bool>(output);
    }

    struct Player
    {
        sf::RectangleShape shape;
        sf::Vector2f velocity{ 0.f, 0.f };
        bool grounded = false;

        Player()
        {
            shape.setSize({ PlayerWidth, PlayerHeight });
            shape.setFillColor(sf::Color(80, 210, 255));
        }
    };

    struct Coin
    {
        sf::CircleShape shape;
        bool active = true;

        explicit Coin(sf::Vector2f centre)
        {
            shape.setRadius(10.f);
            shape.setOrigin({ 10.f, 10.f });
            shape.setPosition(centre);
            shape.setFillColor(sf::Color(255, 210, 60));
        }

        sf::FloatRect collisionBounds() const
        {
            // Keep collision bounds constant while animating.
            const auto position = shape.getPosition();

            return sf::FloatRect(
                position - sf::Vector2f{ 10.f, 10.f },
                { 20.f, 20.f });
        }
    };

    struct Enemy
    {
        sf::RectangleShape shape;

        float leftLimit = 0.f;
        float rightLimit = 0.f;
        float direction = 1.f;
        float speed = 80.f;

        bool active = true;

        explicit Enemy(sf::Vector2f position)
        {
            shape.setSize({ 32.f, 32.f });
            shape.setPosition(position);
            shape.setFillColor(sf::Color(240, 90, 90));
        }
    };

    struct Checkpoint
    {
        sf::FloatRect bounds;
        sf::Vector2f respawn;
        bool active = false;
    };

    struct Level
    {
        std::vector<sf::RectangleShape> platforms;
        std::vector<Coin> coins;
        std::vector<Enemy> enemies;
        std::vector<Checkpoint> checkpoints;

        sf::Vector2f spawn{ 0.f, 0.f };
        sf::FloatRect goalBounds;

        float width = 0.f;
        float height = 0.f;
    };

    std::vector<std::string> makeLevelData()
    {
        std::vector<std::string> grid(
            15, std::string(80, '.'));

        auto fill = [&](int row, int first, int last, char tile)
            {
                for (int column = first; column <= last; ++column)
                    grid[row][column] = tile;
            };

        fill(14, 0, 79, '#');

        fill(14, 18, 19, '.');
        fill(14, 42, 43, '.');

        fill(11, 5, 9, '#');
        fill(8, 11, 15, '#');

        fill(11, 24, 28, '#');
        fill(8, 30, 34, '#');

        fill(11, 48, 52, '#');
        fill(8, 54, 58, '#');

        fill(11, 66, 70, '#');

        grid[13][1] = 'S';
        grid[13][76] = 'G';

        grid[10][7] = 'O';
        grid[7][13] = 'O';
        grid[13][22] = 'O';
        grid[10][26] = 'O';
        grid[7][32] = 'O';
        grid[13][39] = 'O';
        grid[13][46] = 'O';
        grid[10][50] = 'O';
        grid[7][56] = 'O';
        grid[13][72] = 'O';

        grid[13][12] = 'E';
        grid[13][32] = 'E';
        grid[13][60] = 'E';

        grid[13][37] = 'K';

        return grid;
    }

    Level loadLevel(const std::vector<std::string>& grid)
    {
        if (grid.empty() || grid.front().empty())
            throw std::runtime_error("The level is empty.");

        Level level;
        const std::size_t columns = grid.front().size();

        level.width = static_cast<float>(columns) * TileSize;
        level.height = static_cast<float>(grid.size()) * TileSize;

        if (level.width < WindowWidth
            || level.height != WindowHeight)
        {
            throw std::runtime_error(
                "The level must be at least 800 pixels wide "
                "and exactly 600 pixels high.");
        }

        int spawnCount = 0;
        int goalCount = 0;

        for (std::size_t row = 0; row < grid.size(); ++row)
        {
            if (grid[row].size() != columns)
                throw std::runtime_error("Unequal level row widths.");

            for (std::size_t column = 0; column < columns; ++column)
            {
                const char tile = grid[row][column];
                const float x = static_cast<float>(column) * TileSize;
                const float y = static_cast<float>(row) * TileSize;

                const sf::Vector2f playerPosition{
                    x + (TileSize - PlayerWidth) / 2.f,
                    y + TileSize - PlayerHeight
                };

                switch (tile)
                {
                case 'S':
                    ++spawnCount;
                    level.spawn = playerPosition;
                    break;

                case 'G':
                    ++goalCount;
                    level.goalBounds = sf::FloatRect(
                        { x, y - TileSize },
                        { TileSize, TileSize * 2.f });
                    break;

                case 'O':
                    level.coins.emplace_back(
                        sf::Vector2f{ x + 20.f, y + 20.f });
                    break;

                case 'E':
                    level.enemies.emplace_back(
                        sf::Vector2f{ x + 4.f, y + 8.f });
                    break;

                case 'K':
                {
                    Checkpoint checkpoint;
                    checkpoint.bounds = sf::FloatRect(
                        { x, y - TileSize },
                        { TileSize, TileSize * 2.f });
                    checkpoint.respawn = playerPosition;
                    level.checkpoints.push_back(checkpoint);
                    break;
                }

                case '.':
                case '#':
                    break;

                default:
                    throw std::runtime_error("Unknown level symbol.");
                }
            }

            std::size_t column = 0;

            while (column < columns)
            {
                if (grid[row][column] != '#')
                {
                    ++column;
                    continue;
                }

                const std::size_t first = column;

                while (column < columns && grid[row][column] == '#')
                    ++column;

                sf::RectangleShape platform({
                    static_cast<float>(column - first) * TileSize,
                    TileSize
                    });

                platform.setPosition({
                    static_cast<float>(first) * TileSize,
                    static_cast<float>(row) * TileSize
                    });

                platform.setFillColor(
                    row == grid.size() - 1
                    ? sf::Color(70, 145, 80)
                    : sf::Color(110, 180, 110));

                level.platforms.push_back(platform);
            }
        }

        if (spawnCount != 1 || goalCount != 1)
            throw std::runtime_error("Exactly one S and G are required.");

        auto safePosition = [&](sf::Vector2f position)
            {
                const sf::FloatRect body(
                    position, { PlayerWidth, PlayerHeight });

                bool supported = false;

                for (const auto& platform : level.platforms)
                {
                    const auto bounds = platform.getGlobalBounds();

                    if (body.findIntersection(bounds))
                        return false;

                    if (std::abs(
                        position.y + PlayerHeight
                        - bounds.position.y) < 0.1f
                        && position.x >= bounds.position.x
                        && position.x + PlayerWidth
                        <= bounds.position.x + bounds.size.x)
                    {
                        supported = true;
                    }
                }

                return supported;
            };

        if (!safePosition(level.spawn))
            throw std::runtime_error("Unsafe starting position.");

        for (const auto& checkpoint : level.checkpoints)
        {
            if (!safePosition(checkpoint.respawn))
                throw std::runtime_error("Unsafe checkpoint position.");
        }

        for (auto& enemy : level.enemies)
        {
            const auto position = enemy.shape.getPosition();
            bool supported = false;

            for (const auto& platform : level.platforms)
            {
                const auto bounds = platform.getGlobalBounds();

                if (std::abs(
                    position.y + 32.f - bounds.position.y) < 0.1f
                    && position.x >= bounds.position.x
                    && position.x + 32.f
                    <= bounds.position.x + bounds.size.x)
                {
                    enemy.leftLimit = std::max(
                        bounds.position.x, position.x - 100.f);

                    enemy.rightLimit = std::min(
                        bounds.position.x + bounds.size.x - 32.f,
                        position.x + 100.f);

                    supported = true;
                    break;
                }
            }

            if (!supported)
                throw std::runtime_error("Enemy has no supporting platform.");
        }

        return level;
    }

    struct Game
    {
        Audio audio;
        Player player;
        Level level;
        sf::View camera;

        State state = State::Menu;
        sf::Vector2f respawnPosition{ 0.f, 0.f };

        std::filesystem::path scorePath = highScorePath();

        int lives = 3;
        int score = 0;
        int bestScore = 0;
        int collectedCoins = 0;

        float elapsedTime = 0.f;
        float protection = 0.f;
        float jumpBuffer = 0.f;
        float coyoteTime = 0.f;

        bool checkpointReached = false;
        bool saveFailed = false;

        Game()
            : level(loadLevel(makeLevelData())),
            camera(sf::FloatRect(
                { 0.f, 0.f },
                { WindowWidth, WindowHeight }))
        {
            respawnPosition = level.spawn;
            player.shape.setPosition(respawnPosition);
            bestScore = loadHighScore(scorePath);
        }
    };

    void updateCamera(Game& game)
    {
        const float centreX = std::clamp(
            game.player.shape.getPosition().x + PlayerWidth / 2.f,
            WindowWidth / 2.f,
            game.level.width - WindowWidth / 2.f);

        game.camera.setCenter({ centreX, WindowHeight / 2.f });
    }

    void respawnPlayer(Game& game)
    {
        game.player.shape.setPosition(game.respawnPosition);
        game.player.velocity = { 0.f, 0.f };
        game.player.grounded = false;

        game.jumpBuffer = 0.f;
        game.coyoteTime = 0.f;
        game.protection = 1.2f;

        updateCamera(game);
    }

    void resetRun(Game& game)
    {
        game.audio.clear();
        game.audio.setPaused(false);

        game.level = loadLevel(makeLevelData());
        game.respawnPosition = game.level.spawn;

        game.lives = 3;
        game.score = 0;
        game.collectedCoins = 0;
        game.elapsedTime = 0.f;
        game.checkpointReached = false;

        respawnPlayer(game);
        game.protection = 0.f;
    }

    void startGame(Game& game)
    {
        resetRun(game);
        game.state = State::Playing;
    }

    void returnToMenu(Game& game)
    {
        resetRun(game);
        game.state = State::Menu;
    }

    void finishGame(Game& game, State result)
    {
        if (game.state != State::Playing)
            return;

        game.state = result;
        game.jumpBuffer = 0.f;

        if (result == State::Won)
            game.audio.play(Cue::Win);

        if (game.score > game.bestScore)
        {
            game.bestScore = game.score;
            game.saveFailed =
                !saveHighScore(game.scorePath, game.bestScore);
        }
    }

    void loseLife(Game& game)
    {
        if (game.state != State::Playing)
            return;

        game.audio.play(Cue::Damage);
        --game.lives;

        if (game.lives <= 0)
        {
            game.player.velocity = { 0.f, 0.f };
            finishGame(game, State::GameOver);
        }
        else
        {
            respawnPlayer(game);
        }
    }

    void moveHorizontally(Game& game, float dt)
    {
        auto& player = game.player;
        const float movement = player.velocity.x * dt;

        if (movement == 0.f)
            return;

        player.shape.move({ movement, 0.f });

        for (const auto& platform : game.level.platforms)
        {
            const auto bounds = platform.getGlobalBounds();

            if (!player.shape.getGlobalBounds().findIntersection(bounds))
                continue;

            auto position = player.shape.getPosition();

            position.x = movement > 0.f
                ? bounds.position.x - PlayerWidth
                : bounds.position.x + bounds.size.x;

            player.shape.setPosition(position);
            player.velocity.x = 0.f;
        }

        auto position = player.shape.getPosition();

        position.x = std::clamp(
            position.x, 0.f, game.level.width - PlayerWidth);

        player.shape.setPosition(position);
    }

    void moveVertically(Game& game, float dt)
    {
        auto& player = game.player;

        player.velocity.y = std::min(
            player.velocity.y + Gravity * dt,
            MaximumFallSpeed);

        const float movement = player.velocity.y * dt;

        player.grounded = false;
        player.shape.move({ 0.f, movement });

        for (const auto& platform : game.level.platforms)
        {
            const auto bounds = platform.getGlobalBounds();

            if (!player.shape.getGlobalBounds().findIntersection(bounds))
                continue;

            auto position = player.shape.getPosition();

            if (movement > 0.f)
            {
                position.y = bounds.position.y - PlayerHeight;
                player.grounded = true;
            }
            else if (movement < 0.f)
            {
                position.y = bounds.position.y + bounds.size.y;
            }

            player.shape.setPosition(position);
            player.velocity.y = 0.f;
        }
    }

    void updateEnemies(Game& game, float dt)
    {
        for (auto& enemy : game.level.enemies)
        {
            if (!enemy.active)
                continue;

            auto position = enemy.shape.getPosition();

            position.x += enemy.direction * enemy.speed * dt;

            if (position.x <= enemy.leftLimit)
            {
                position.x = enemy.leftLimit;
                enemy.direction = 1.f;
            }
            else if (position.x >= enemy.rightLimit)
            {
                position.x = enemy.rightLimit;
                enemy.direction = -1.f;
            }

            enemy.shape.setPosition(position);
        }
    }

    bool checkEnemies(Game& game, const sf::FloatRect& previousBounds)
    {
        for (auto& enemy : game.level.enemies)
        {
            if (!enemy.active)
                continue;

            const auto bounds = enemy.shape.getGlobalBounds();

            if (!game.player.shape.getGlobalBounds().findIntersection(bounds))
                continue;

            const float previousFeet =
                previousBounds.position.y + previousBounds.size.y;

            if (game.player.velocity.y > 0.f
                && previousFeet <= bounds.position.y + 0.1f)
            {
                enemy.active = false;
                game.score += 100;
                game.audio.play(Cue::Stomp);

                auto position = game.player.shape.getPosition();
                position.y = bounds.position.y - PlayerHeight;

                game.player.shape.setPosition(position);
                game.player.velocity.y = -360.f;
                game.player.grounded = false;
                game.coyoteTime = 0.f;

                break;
            }

            if (game.protection <= 0.f)
            {
                loseLife(game);
                return true;
            }
        }

        return false;
    }

    void collectCoins(Game& game)
    {
        const auto bounds = game.player.shape.getGlobalBounds();

        for (auto& coin : game.level.coins)
        {
            if (coin.active && bounds.findIntersection(coin.collisionBounds()))
            {
                coin.active = false;
                ++game.collectedCoins;
                game.score += 50;
                game.audio.play(Cue::Coin);
            }
        }
    }

    void activateCheckpoints(Game& game)
    {
        const auto bounds = game.player.shape.getGlobalBounds();

        for (auto& checkpoint : game.level.checkpoints)
        {
            if (!checkpoint.active && bounds.findIntersection(checkpoint.bounds))
            {
                checkpoint.active = true;
                game.respawnPosition = checkpoint.respawn;
                game.checkpointReached = true;
                game.audio.play(Cue::Checkpoint);
            }
        }
    }

    void updateGame(Game& game, float dt)
    {
        game.elapsedTime += dt;
        game.protection = std::max(0.f, game.protection - dt);

        if (game.player.grounded)
            game.coyoteTime = 0.08f;
        else
            game.coyoteTime = std::max(0.f, game.coyoteTime - dt);

        const auto previousBounds =
            game.player.shape.getGlobalBounds();

        float input = 0.f;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left))
            input -= 1.f;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right))
            input += 1.f;

        game.player.velocity.x = input * MoveSpeed;

        if (game.jumpBuffer > 0.f && game.coyoteTime > 0.f)
        {
            game.player.velocity.y = -JumpSpeed;
            game.player.grounded = false;

            game.jumpBuffer = 0.f;
            game.coyoteTime = 0.f;

            game.audio.play(Cue::Jump);
        }

        game.jumpBuffer = std::max(0.f, game.jumpBuffer - dt);

        moveHorizontally(game, dt);
        moveVertically(game, dt);
        updateEnemies(game, dt);

        for (auto& coin : game.level.coins)
        {
            const float phase =
                game.elapsedTime * 4.f + coin.shape.getPosition().x * 0.01f;

            coin.shape.setScale({
                0.35f + 0.65f * std::abs(std::cos(phase)),
                1.f
                });
        }

        if (game.player.shape.getPosition().y > game.level.height + 100.f)
        {
            loseLife(game);
            updateCamera(game);
            return;
        }

        if (checkEnemies(game, previousBounds))
        {
            updateCamera(game);
            return;
        }

        collectCoins(game);
        activateCheckpoints(game);

        if (game.player.shape.getGlobalBounds()
            .findIntersection(game.level.goalBounds))
        {
            finishGame(game, State::Won);
        }

        updateCamera(game);
    }

    std::string timeString(float seconds)
    {
        std::ostringstream text;
        text << std::fixed << std::setprecision(1) << seconds << "s";
        return text.str();
    }

    void drawText(
        sf::RenderTarget& target,
        const sf::Font& font,
        const std::string& message,
        unsigned int size,
        float x,
        float y,
        sf::Color colour = sf::Color::White,
        bool centred = false)
    {
        sf::Text text(font);
        text.setString(message);
        text.setCharacterSize(size);
        text.setFillColor(colour);

        if (centred)
        {
            const auto bounds = text.getLocalBounds();

            text.setOrigin({
                bounds.position.x + bounds.size.x / 2.f,
                bounds.position.y + bounds.size.y / 2.f
                });
        }

        text.setPosition({ x, y });
        target.draw(text);
    }

    void drawFlag(
        sf::RenderWindow& window,
        const sf::FloatRect& bounds,
        sf::Color colour)
    {
        sf::RectangleShape pole({ 5.f, bounds.size.y });
        pole.setPosition({ bounds.position.x + 5.f, bounds.position.y });
        pole.setFillColor(sf::Color::White);

        sf::RectangleShape flag({ 30.f, 22.f });
        flag.setPosition({ bounds.position.x + 10.f, bounds.position.y });
        flag.setFillColor(colour);

        window.draw(pole);
        window.draw(flag);
    }

    void drawPanel(sf::RenderWindow& window)
    {
        sf::RectangleShape panel({ WindowWidth, WindowHeight });
        panel.setFillColor(sf::Color(0, 0, 0, 190));
        window.draw(panel);
    }

    void drawInterface(
        sf::RenderWindow& window,
        const sf::Font& font,
        const Game& game)
    {
        const auto cyan = sf::Color(100, 230, 255);

        if (game.state == State::Menu)
        {
            drawPanel(window);

            drawText(window, font, "PLATFORM ADVENTURE",
                40, 400.f, 110.f, cyan, true);

            drawText(window, font, "Reach the yellow finish flag",
                24, 400.f, 190.f, sf::Color::White, true);

            drawText(window, font, "Collect coins and stomp enemies",
                22, 400.f, 235.f, sf::Color::White, true);

            drawText(window, font,
                "Best score: " + std::to_string(game.bestScore),
                26, 400.f, 300.f, sf::Color(255, 210, 60), true);

            drawText(window, font, "Enter: Start",
                28, 400.f, 365.f, sf::Color::White, true);

            drawText(window, font, "Arrows: Move    Space: Jump",
                20, 400.f, 430.f, sf::Color::White, true);

            drawText(window, font, "P: Pause    R: Restart    M: Menu",
                20, 400.f, 470.f, sf::Color::White, true);
        }
        else
        {
            sf::RectangleShape hud({ WindowWidth, 66.f });
            hud.setFillColor(sf::Color(0, 0, 0, 190));
            window.draw(hud);

            drawText(window, font,
                "Lives: " + std::to_string(game.lives),
                19, 12.f, 6.f);

            drawText(window, font,
                "Score: " + std::to_string(game.score),
                19, 125.f, 6.f);

            drawText(window, font,
                "Coins: " + std::to_string(game.collectedCoins)
                + "/" + std::to_string(game.level.coins.size()),
                19, 285.f, 6.f);

            drawText(window, font,
                "Best: " + std::to_string(game.bestScore),
                19, 445.f, 6.f);

            drawText(window, font,
                "Time: " + timeString(game.elapsedTime),
                19, 610.f, 6.f);

            drawText(window, font,
                game.checkpointReached
                ? "Checkpoint: Active"
                : "Checkpoint: Start",
                17, 12.f, 36.f, cyan);

            if (game.state == State::Paused)
            {
                drawPanel(window);

                drawText(window, font, "PAUSED",
                    44, 400.f, 245.f, cyan, true);

                drawText(window, font, "P: Resume    M: Menu",
                    24, 400.f, 315.f, sf::Color::White, true);
            }

            if (game.state == State::Won || game.state == State::GameOver)
            {
                drawPanel(window);

                drawText(window, font,
                    game.state == State::Won
                    ? "ADVENTURE COMPLETE"
                    : "GAME OVER",
                    38, 400.f, 180.f, cyan, true);

                drawText(window, font,
                    "Score: " + std::to_string(game.score),
                    28, 400.f, 250.f, sf::Color::White, true);

                drawText(window, font,
                    "Coins: " + std::to_string(game.collectedCoins)
                    + "/" + std::to_string(game.level.coins.size()),
                    24, 400.f, 295.f, sf::Color::White, true);

                drawText(window, font,
                    "Time: " + timeString(game.elapsedTime),
                    24, 400.f, 335.f, sf::Color::White, true);

                drawText(window, font,
                    "Best: " + std::to_string(game.bestScore),
                    24, 400.f, 375.f,
                    sf::Color(255, 210, 60), true);

                drawText(window, font, "Enter: Play again    M: Menu",
                    22, 400.f, 445.f, sf::Color::White, true);
            }
        }

        drawText(
            window, font,
            game.audio.isMuted() ? "V: Sound off" : "V: Sound on",
            17, 630.f, 565.f);

        if (game.saveFailed)
        {
            drawText(window, font, "High score could not be saved.",
                17, 15.f, 565.f, sf::Color(255, 150, 100));
        }
    }

    void drawGame(
        sf::RenderWindow& window,
        const sf::Font& font,
        Game& game)
    {
        window.clear(sf::Color(25, 35, 60));
        window.setView(game.camera);

        for (const auto& platform : game.level.platforms)
            window.draw(platform);

        for (const auto& coin : game.level.coins)
        {
            if (coin.active)
                window.draw(coin.shape);
        }

        for (const auto& checkpoint : game.level.checkpoints)
        {
            drawFlag(
                window, checkpoint.bounds,
                checkpoint.active
                ? sf::Color(70, 255, 140)
                : sf::Color(150, 150, 160));
        }

        drawFlag(window, game.level.goalBounds, sf::Color(255, 210, 70));

        for (const auto& enemy : game.level.enemies)
        {
            if (enemy.active)
                window.draw(enemy.shape);
        }

        if (game.state != State::Menu)
        {
            sf::Color colour(80, 210, 255);

            if (game.protection > 0.f
                && static_cast<int>(game.protection * 12.f) % 2 == 0)
            {
                colour.a = 90;
            }

            game.player.shape.setFillColor(colour);
            window.draw(game.player.shape);
        }

        window.setView(window.getDefaultView());
        drawInterface(window, font, game);
        window.display();
    }
} // namespace

int main()
{
    try
    {
        sf::RenderWindow window(
            sf::VideoMode({ 800u, 600u }),
            "PlatformAdventure",
            sf::Style::Titlebar | sf::Style::Close);

        window.setVerticalSyncEnabled(true);
        window.setKeyRepeatEnabled(false);

        const auto fontPath =
            std::filesystem::path(GAME_ASSET_DIR)
            / "fonts" / "welcome.ttf";

        sf::Font font;

        if (!font.openFromFile(fontPath))
        {
            std::cerr << "Unable to load the font.\n"
                << "Expected location: " << fontPath << '\n';
            return 1;
        }

        Game game;
        updateCamera(game);

        sf::Clock clock;
        float accumulator = 0.f;

        while (window.isOpen())
        {
            bool resetTiming = false;

            while (const auto event = window.pollEvent())
            {
                if (event->is<sf::Event::Closed>())
                {
                    window.close();
                    continue;
                }

                if (event->is<sf::Event::FocusLost>()
                    && game.state == State::Playing)
                {
                    game.state = State::Paused;
                    game.jumpBuffer = 0.f;
                    resetTiming = true;
                }

                const auto* key = event->getIf<sf::Event::KeyPressed>();

                if (!key)
                    continue;

                if (key->code == sf::Keyboard::Key::Escape)
                {
                    window.close();
                }
                else if (key->code == sf::Keyboard::Key::V)
                {
                    game.audio.toggleMute();
                }
                else if (key->code == sf::Keyboard::Key::M)
                {
                    returnToMenu(game);
                    resetTiming = true;
                }
                else if (key->code == sf::Keyboard::Key::Enter
                    && (game.state == State::Menu
                        || game.state == State::Won
                        || game.state == State::GameOver))
                {
                    startGame(game);
                    resetTiming = true;
                }
                else if (key->code == sf::Keyboard::Key::R
                    && game.state != State::Menu)
                {
                    startGame(game);
                    resetTiming = true;
                }
                else if (key->code == sf::Keyboard::Key::P)
                {
                    if (game.state == State::Playing)
                    {
                        game.state = State::Paused;
                        resetTiming = true;
                    }
                    else if (game.state == State::Paused && window.hasFocus())
                    {
                        game.state = State::Playing;
                        resetTiming = true;
                    }

                    game.jumpBuffer = 0.f;
                }
                else if (key->code == sf::Keyboard::Key::Space
                    && game.state == State::Playing)
                {
                    game.jumpBuffer = 0.12f;
                }
            }

            if (!window.isOpen())
                break;

            game.audio.setPaused(game.state == State::Paused);
            game.audio.cleanup();

            float elapsed =
                std::min(clock.restart().asSeconds(), 0.1f);

            if (resetTiming)
            {
                elapsed = 0.f;
                accumulator = 0.f;
            }

            if (game.state != State::Playing)
            {
                accumulator = 0.f;
            }
            else
            {
                accumulator += elapsed;

                while (accumulator >= FixedStep
                    && game.state == State::Playing)
                {
                    updateGame(game, FixedStep);
                    accumulator -= FixedStep;
                }

                if (game.state != State::Playing)
                    accumulator = 0.f;
            }

            drawGame(window, font, game);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "PlatformAdventure error: "
            << error.what() << '\n';
        return 1;
    }

    return 0;
}
