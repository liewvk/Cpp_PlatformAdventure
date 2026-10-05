#include <SFML/Graphics.hpp>

#include <algorithm>
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
        Won
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

    struct Level
    {
        std::vector<sf::RectangleShape> platforms;

        sf::Vector2f spawn{ 0.f, 0.f };
        sf::FloatRect goalBounds;

        float width = 0.f;
        float height = 0.f;
    };

    std::vector<std::string> makeLevelData()
    {
        constexpr int Rows = 15;
        constexpr int Columns = 80;

        std::vector<std::string> grid(
            Rows, std::string(Columns, '.'));

        auto fill = [&](int row, int first, int last, char tile)
            {
                for (int column = first; column <= last; ++column)
                    grid[row][column] = tile;
            };

        // Main floor.
        fill(14, 0, 79, '#');

        // Two gaps, each 80 pixels wide.
        fill(14, 18, 19, '.');
        fill(14, 42, 43, '.');

        // Optional elevated routes.
        fill(11, 5, 9, '#');
        fill(8, 11, 15, '#');

        fill(11, 24, 28, '#');
        fill(8, 30, 34, '#');

        fill(11, 48, 52, '#');
        fill(8, 54, 58, '#');

        fill(11, 66, 70, '#');

        // Start and finish markers.
        grid[13][1] = 'S';
        grid[13][76] = 'G';

        return grid;
    }

    Level loadLevel(const std::vector<std::string>& grid)
    {
        if (grid.empty() || grid.front().empty())
            throw std::runtime_error("The level is empty.");

        const std::size_t columns = grid.front().size();

        int spawnCount = 0;
        int goalCount = 0;

        Level level;

        level.width =
            static_cast<float>(columns) * TileSize;

        level.height =
            static_cast<float>(grid.size()) * TileSize;

        if (level.width < WindowWidth ||
            level.height != WindowHeight)
        {
            throw std::runtime_error(
                "This chapter requires a level at least "
                "800 pixels wide and exactly 600 pixels high.");
        }

        for (std::size_t row = 0; row < grid.size(); ++row)
        {
            if (grid[row].size() != columns)
            {
                throw std::runtime_error(
                    "Every level row must have the same width.");
            }

            for (std::size_t column = 0; column < columns; ++column)
            {
                const char tile = grid[row][column];

                const float x =
                    static_cast<float>(column) * TileSize;

                const float y =
                    static_cast<float>(row) * TileSize;

                if (tile == 'S')
                {
                    ++spawnCount;

                    // Align the player's feet with the
                    // bottom edge of the start cell.
                    level.spawn = {
                        x + (TileSize - PlayerWidth) / 2.f,
                        y + TileSize - PlayerHeight
                    };
                }
                else if (tile == 'G')
                {
                    ++goalCount;

                    // The finish area is two cells tall.
                    level.goalBounds = sf::FloatRect(
                        { x, y - TileSize },
                        { TileSize, TileSize * 2.f });
                }
                else if (tile != '.' && tile != '#')
                {
                    throw std::runtime_error(
                        "The level contains an unknown character.");
                }
            }

            // Merge each horizontal run of solid cells.
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

                const float width =
                    static_cast<float>(column - first) * TileSize;

                sf::RectangleShape platform(
                    { width, TileSize });

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

        const sf::FloatRect playerStart(
            level.spawn, { PlayerWidth, PlayerHeight });

        for (const auto& platform : level.platforms)
        {
            if (playerStart.findIntersection(
                platform.getGlobalBounds()))
            {
                throw std::runtime_error(
                    "The player starts inside a solid platform.");
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
        bool jumpRequested = false;

        int falls = 0;
        float elapsedTime = 0.f;

        Game()
            : level(loadLevel(makeLevelData())),
            camera(sf::FloatRect(
                { 0.f, 0.f },
                { WindowWidth, WindowHeight }))
        {
            player.shape.setPosition(level.spawn);
        }
    };

    void updateCamera(Game& game)
    {
        const float playerCentreX =
            game.player.shape.getPosition().x
            + PlayerWidth / 2.f;

        const float centreX = std::clamp(
            playerCentreX,
            WindowWidth / 2.f,
            game.level.width - WindowWidth / 2.f);

        game.camera.setCenter({
            centreX,
            WindowHeight / 2.f
            });
    }

    void respawnPlayer(Game& game)
    {
        game.player.shape.setPosition(game.level.spawn);
        game.player.velocity = { 0.f, 0.f };
        game.player.grounded = false;

        game.jumpRequested = false;
        updateCamera(game);
    }

    void restartGame(Game& game)
    {
        game.state = State::Playing;
        game.falls = 0;
        game.elapsedTime = 0.f;

        respawnPlayer(game);
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
            const auto platformBounds =
                platform.getGlobalBounds();

            if (!player.shape.getGlobalBounds()
                .findIntersection(platformBounds))
            {
                continue;
            }

            auto position = player.shape.getPosition();

            if (movement > 0.f)
            {
                position.x =
                    platformBounds.position.x - PlayerWidth;
            }
            else
            {
                position.x =
                    platformBounds.position.x
                    + platformBounds.size.x;
            }

            player.shape.setPosition(position);
            player.velocity.x = 0.f;
        }

        auto position = player.shape.getPosition();

        position.x = std::clamp(
            position.x,
            0.f,
            game.level.width - PlayerWidth);

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
            const auto platformBounds =
                platform.getGlobalBounds();

            if (!player.shape.getGlobalBounds()
                .findIntersection(platformBounds))
            {
                continue;
            }

            auto position = player.shape.getPosition();

            if (movement > 0.f)
            {
                position.y =
                    platformBounds.position.y - PlayerHeight;

                player.grounded = true;
            }
            else if (movement < 0.f)
            {
                position.y =
                    platformBounds.position.y
                    + platformBounds.size.y;
            }

            player.shape.setPosition(position);
            player.velocity.y = 0.f;
        }
    }

    void updateGame(Game& game, float dt)
    {
        game.elapsedTime += dt;

        Player& player = game.player;

        float horizontalInput = 0.f;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left))
            horizontalInput -= 1.f;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right))
            horizontalInput += 1.f;

        player.velocity.x = horizontalInput * MoveSpeed;

        if (game.jumpRequested && player.grounded)
        {
            player.velocity.y = -JumpSpeed;
            player.grounded = false;
        }

        game.jumpRequested = false;

        moveHorizontally(game, dt);
        moveVertically(game, dt);

        if (player.shape.getPosition().y
            > game.level.height + 100.f)
        {
            ++game.falls;
            respawnPlayer(game);
            return;
        }

        if (player.shape.getGlobalBounds()
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
        {
            title << "LEVEL COMPLETE! | R: Play again";
        }
        else if (game.state == State::Paused)
        {
            title << "PAUSED | P: Resume | R: Restart";
        }
        else
        {
            title << "Left/Right: Move | Space: Jump"
                << " | P: Pause | R: Restart";
        }

        title << " | Falls: " << game.falls
            << " | Time: " << std::fixed << std::setprecision(1)
            << game.elapsedTime << "s"
            << " | Escape: Exit";

        window.setTitle(title.str());
    }

    void drawGoal(sf::RenderWindow& window, const Level& level)
    {
        const auto position = level.goalBounds.position;
        const auto size = level.goalBounds.size;

        sf::RectangleShape pole({ 5.f, size.y });
        pole.setPosition({
            position.x + 5.f,
            position.y
            });
        pole.setFillColor(sf::Color::White);

        sf::RectangleShape flag({ 30.f, 22.f });
        flag.setPosition({
            position.x + 10.f,
            position.y
            });
        flag.setFillColor(sf::Color(255, 210, 70));

        window.draw(pole);
        window.draw(flag);
    }

    void drawGame(sf::RenderWindow& window, const Game& game)
    {
        window.clear(sf::Color(25, 35, 60));

        // World objects use the scrolling camera.
        window.setView(game.camera);

        for (const auto& platform : game.level.platforms)
            window.draw(platform);

        drawGoal(window, game.level);
        window.draw(game.player.shape);

        // Screen overlays use the fixed default view.
        window.setView(window.getDefaultView());

        if (game.state != State::Playing)
        {
            sf::RectangleShape overlay(
                { WindowWidth, WindowHeight });

            overlay.setFillColor(
                game.state == State::Won
                ? sf::Color(20, 100, 50, 80)
                : sf::Color(0, 0, 0, 130));

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
