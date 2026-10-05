#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
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

    enum class State
    {
        Playing,
        Paused,
        Won,
        GameOver
    };

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

        // Ten coins on the floor and elevated routes.
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

        // Three enemies on solid sections of floor.
        grid[13][12] = 'E';
        grid[13][32] = 'E';
        grid[13][60] = 'E';

        // Checkpoint before the second gap.
        grid[13][37] = 'K';

        return grid;
    }

    Level loadLevel(const std::vector<std::string>& grid)
    {
        if (grid.empty() || grid.front().empty())
            throw std::runtime_error("The level is empty.");

        Level level;

        const std::size_t columns = grid.front().size();

        level.width =
            static_cast<float>(columns) * TileSize;

        level.height =
            static_cast<float>(grid.size()) * TileSize;

        if (level.width < WindowWidth ||
            level.height != WindowHeight)
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
            {
                throw std::runtime_error(
                    "All level rows must have the same width.");
            }

            for (std::size_t column = 0;
                column < columns;
                ++column)
            {
                const char tile = grid[row][column];

                const float x =
                    static_cast<float>(column) * TileSize;

                const float y =
                    static_cast<float>(row) * TileSize;

                const sf::Vector2f playerPosition{
                    x + (TileSize - PlayerWidth) / 2.f,
                    y + TileSize - PlayerHeight
                };

                if (tile == 'S')
                {
                    ++spawnCount;
                    level.spawn = playerPosition;
                }
                else if (tile == 'G')
                {
                    ++goalCount;

                    level.goalBounds = sf::FloatRect(
                        { x, y - TileSize },
                        { TileSize, TileSize * 2.f });
                }
                else if (tile == 'O')
                {
                    level.coins.emplace_back(
                        sf::Vector2f{
                            x + TileSize / 2.f,
                            y + TileSize / 2.f });
                }
                else if (tile == 'E')
                {
                    level.enemies.emplace_back(
                        sf::Vector2f{ x + 4.f, y + 8.f });
                }
                else if (tile == 'K')
                {
                    Checkpoint checkpoint;

                    checkpoint.bounds = sf::FloatRect(
                        { x, y - TileSize },
                        { TileSize, TileSize * 2.f });

                    checkpoint.respawn = playerPosition;

                    level.checkpoints.push_back(checkpoint);
                }
                else if (tile != '.' && tile != '#')
                {
                    throw std::runtime_error(
                        "Unknown character in level data.");
                }
            }

            // Combine each horizontal run of solid tiles.
            std::size_t column = 0;

            while (column < columns)
            {
                if (grid[row][column] != '#')
                {
                    ++column;
                    continue;
                }

                const std::size_t first = column;

                while (column < columns &&
                    grid[row][column] == '#')
                {
                    ++column;
                }

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
        {
            throw std::runtime_error(
                "The level needs exactly one S and one G.");
        }

        auto safeRespawn = [&](sf::Vector2f position)
            {
                const sf::FloatRect body(
                    position, { PlayerWidth, PlayerHeight });

                bool supported = false;

                for (const auto& platform : level.platforms)
                {
                    const auto bounds = platform.getGlobalBounds();

                    if (body.findIntersection(bounds))
                        return false;

                    const float feet = position.y + PlayerHeight;

                    if (std::abs(feet - bounds.position.y) < 0.1f
                        && position.x >= bounds.position.x
                        && position.x + PlayerWidth
                        <= bounds.position.x + bounds.size.x)
                    {
                        supported = true;
                    }
                }

                return supported;
            };

        if (!safeRespawn(level.spawn))
            throw std::runtime_error("The start is not safe.");

        for (const auto& checkpoint : level.checkpoints)
        {
            if (!safeRespawn(checkpoint.respawn))
            {
                throw std::runtime_error(
                    "A checkpoint respawn position is not safe.");
            }
        }

        // Set patrol limits within each enemy's supporting
        // platform, so enemies do not walk into floor gaps.
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
            {
                throw std::runtime_error(
                    "An enemy needs a supporting platform.");
            }
        }

        return level;
    }

    struct Game
    {
        Player player;
        Level level;
        sf::View camera;

        State state = State::Playing;

        sf::Vector2f respawnPosition{ 0.f, 0.f };

        int lives = 3;
        int score = 0;
        int collectedCoins = 0;

        float elapsedTime = 0.f;
        float protection = 0.f;

        bool jumpRequested = false;
        bool checkpointReached = false;

        Game()
            : level(loadLevel(makeLevelData())),
            camera(sf::FloatRect(
                { 0.f, 0.f },
                { WindowWidth, WindowHeight }))
        {
            respawnPosition = level.spawn;
            player.shape.setPosition(respawnPosition);
        }
    };

    void updateCamera(Game& game)
    {
        const float centreX = std::clamp(
            game.player.shape.getPosition().x + PlayerWidth / 2.f,
            WindowWidth / 2.f,
            game.level.width - WindowWidth / 2.f);

        game.camera.setCenter({
            centreX, WindowHeight / 2.f
            });
    }

    void respawnPlayer(Game& game)
    {
        game.player.shape.setPosition(game.respawnPosition);
        game.player.velocity = { 0.f, 0.f };
        game.player.grounded = false;

        game.jumpRequested = false;
        game.protection = 1.2f;

        updateCamera(game);
    }

    void restartGame(Game& game)
    {
        game.level = loadLevel(makeLevelData());

        game.state = State::Playing;
        game.respawnPosition = game.level.spawn;

        game.lives = 3;
        game.score = 0;
        game.collectedCoins = 0;
        game.elapsedTime = 0.f;
        game.checkpointReached = false;

        respawnPlayer(game);
        game.protection = 0.f;
    }

    void loseLife(Game& game)
    {
        if (game.state != State::Playing)
            return;

        --game.lives;
        game.jumpRequested = false;

        if (game.lives <= 0)
        {
            game.state = State::GameOver;
            game.player.velocity = { 0.f, 0.f };
        }
        else
        {
            respawnPlayer(game);
        }
    }

    void moveHorizontally(Game& game, float dt)
    {
        Player& player = game.player;
        const float movement = player.velocity.x * dt;

        if (movement == 0.f)
            return;

        player.shape.move({ movement, 0.f });

        for (const auto& platform : game.level.platforms)
        {
            const auto bounds = platform.getGlobalBounds();

            if (!player.shape.getGlobalBounds()
                .findIntersection(bounds))
            {
                continue;
            }

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
        Player& player = game.player;

        player.velocity.y = std::min(
            player.velocity.y + Gravity * dt,
            MaximumFallSpeed);

        const float movement = player.velocity.y * dt;

        player.grounded = false;
        player.shape.move({ 0.f, movement });

        for (const auto& platform : game.level.platforms)
        {
            const auto bounds = platform.getGlobalBounds();

            if (!player.shape.getGlobalBounds()
                .findIntersection(bounds))
            {
                continue;
            }

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

    // Returns true if a harmful collision ended this update.
    bool checkEnemyCollisions(
        Game& game,
        const sf::FloatRect& previousPlayerBounds)
    {
        for (auto& enemy : game.level.enemies)
        {
            if (!enemy.active)
                continue;

            const auto enemyBounds =
                enemy.shape.getGlobalBounds();

            if (!game.player.shape.getGlobalBounds()
                .findIntersection(enemyBounds))
            {
                continue;
            }

            const float previousFeet =
                previousPlayerBounds.position.y
                + previousPlayerBounds.size.y;

            const bool stomp =
                game.player.velocity.y > 0.f
                && previousFeet <= enemyBounds.position.y + 0.1f;

            if (stomp)
            {
                enemy.active = false;
                game.score += 100;

                auto position = game.player.shape.getPosition();
                position.y = enemyBounds.position.y - PlayerHeight;

                game.player.shape.setPosition(position);
                game.player.velocity.y = -360.f;
                game.player.grounded = false;

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
        const auto playerBounds =
            game.player.shape.getGlobalBounds();

        for (auto& coin : game.level.coins)
        {
            if (coin.active
                && playerBounds.findIntersection(
                    coin.shape.getGlobalBounds()))
            {
                coin.active = false;
                ++game.collectedCoins;
                game.score += 50;
            }
        }
    }

    void activateCheckpoints(Game& game)
    {
        const auto playerBounds =
            game.player.shape.getGlobalBounds();

        for (auto& checkpoint : game.level.checkpoints)
        {
            if (!checkpoint.active
                && playerBounds.findIntersection(checkpoint.bounds))
            {
                checkpoint.active = true;
                game.respawnPosition = checkpoint.respawn;
                game.checkpointReached = true;
            }
        }
    }

    void updateGame(Game& game, float dt)
    {
        game.elapsedTime += dt;

        game.protection =
            std::max(0.f, game.protection - dt);

        const auto previousPlayerBounds =
            game.player.shape.getGlobalBounds();

        float input = 0.f;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left))
            input -= 1.f;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right))
            input += 1.f;

        game.player.velocity.x = input * MoveSpeed;

        if (game.jumpRequested && game.player.grounded)
        {
            game.player.velocity.y = -JumpSpeed;
            game.player.grounded = false;
        }

        game.jumpRequested = false;

        moveHorizontally(game, dt);
        moveVertically(game, dt);
        updateEnemies(game, dt);

        if (game.player.shape.getPosition().y
                > game.level.height + 100.f)
        {
            loseLife(game);
            updateCamera(game);
            return;
        }

        if (checkEnemyCollisions(game, previousPlayerBounds))
        {
            updateCamera(game);
            return;
        }

        collectCoins(game);
        activateCheckpoints(game);

        if (game.player.shape.getGlobalBounds()
            .findIntersection(game.level.goalBounds))
        {
            game.state = State::Won;
            game.jumpRequested = false;
        }

        updateCamera(game);
    }

    void updateTitle(
        sf::RenderWindow& window,
        const Game& game)
    {
        std::ostringstream title;

        title << "PlatformAdventure | ";

        if (game.state == State::Won)
            title << "LEVEL COMPLETE! R: Restart";
        else if (game.state == State::GameOver)
            title << "GAME OVER - R: Restart";
        else if (game.state == State::Paused)
            title << "PAUSED - P: Resume";
        else
            title << "Arrows: Move | Space: Jump | P: Pause | R: Restart";

        title << " | Lives: " << game.lives
            << " | Score: " << game.score
            << " | Coins: " << game.collectedCoins
            << "/" << game.level.coins.size()
            << " | Checkpoint: "
            << (game.checkpointReached ? "Active" : "Start")
            << " | Time: " << std::fixed << std::setprecision(1)
            << game.elapsedTime << "s";

        window.setTitle(title.str());
    }

    void drawFlag(
        sf::RenderWindow& window,
        const sf::FloatRect& bounds,
        sf::Color colour)
    {
        sf::RectangleShape pole({ 5.f, bounds.size.y });
        pole.setPosition({
            bounds.position.x + 5.f,
            bounds.position.y
            });
        pole.setFillColor(sf::Color::White);

        sf::RectangleShape flag({ 30.f, 22.f });
        flag.setPosition({
            bounds.position.x + 10.f,
            bounds.position.y
            });
        flag.setFillColor(colour);

        window.draw(pole);
        window.draw(flag);
    }

    void drawGame(sf::RenderWindow& window, Game& game)
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
                window,
                checkpoint.bounds,
                checkpoint.active
                ? sf::Color(70, 255, 140)
                : sf::Color(150, 150, 160));
        }

        drawFlag(
            window,
            game.level.goalBounds,
            sf::Color(255, 210, 70));

        for (const auto& enemy : game.level.enemies)
        {
            if (enemy.active)
                window.draw(enemy.shape);
        }

        sf::Color playerColour = sf::Color(80, 210, 255);

        if (game.protection > 0.f
            && static_cast<int>(game.protection * 12.f) % 2 == 0)
        {
            playerColour.a = 90;
        }

        game.player.shape.setFillColor(playerColour);
        window.draw(game.player.shape);

        window.setView(window.getDefaultView());

        if (game.state != State::Playing)
        {
            sf::RectangleShape overlay(
                { WindowWidth, WindowHeight });

            if (game.state == State::Won)
                overlay.setFillColor(sf::Color(20, 100, 50, 80));
            else if (game.state == State::GameOver)
                overlay.setFillColor(sf::Color(130, 20, 20, 100));
            else
                overlay.setFillColor(sf::Color(0, 0, 0, 130));

            window.draw(overlay);
        }

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
                    game.jumpRequested = false;
                    resetTiming = true;
                }

                const auto* key =
                    event->getIf<sf::Event::KeyPressed>();

                if (!key)
                    continue;

                if (key->code == sf::Keyboard::Key::Escape)
                {
                    window.close();
                }
                else if (key->code == sf::Keyboard::Key::R)
                {
                    restartGame(game);
                    resetTiming = true;
                }
                else if (key->code == sf::Keyboard::Key::P)
                {
                    if (game.state == State::Playing)
                    {
                        game.state = State::Paused;
                        resetTiming = true;
                    }
                    else if (game.state == State::Paused
                        && window.hasFocus())
                    {
                        game.state = State::Playing;
                        resetTiming = true;
                    }

                    game.jumpRequested = false;
                }
                else if (key->code == sf::Keyboard::Key::Space
                    && game.state == State::Playing)
                {
                    game.jumpRequested = true;
                }
            }

            if (!window.isOpen())
                break;

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

            updateTitle(window, game);
            drawGame(window, game);
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
