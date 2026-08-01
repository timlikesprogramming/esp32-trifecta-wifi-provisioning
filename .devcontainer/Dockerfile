FROM mcr.microsoft.com/devcontainers/python:3.12

# Install PlatformIO CLI
RUN pip3 install --no-cache-dir platformio

# Install GitHub CLI (gh)
RUN mkdir -p -m 755 /etc/apt/keyrings \
    && wget -qO- https://cli.github.com/packages/githubcli-archive-keyring.gpg | tee /etc/apt/keyrings/githubcli-archive-keyring.gpg > /dev/null \
    && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | tee /etc/apt/sources.list.d/github-cli.list > /dev/null \
    && apt-get update \
    && apt-get install -y gh \
    && rm -rf /var/lib/apt/lists/*

# Add vscode user to dialout group for serial port access (/dev/ttyACM*, /dev/ttyUSB*)
RUN usermod -aG dialout vscode

# Switch to the non-root user (automatically created by the base image)
USER vscode

# Install Antigravity CLI (agy)
RUN curl -fsSL https://antigravity.google/cli/install.sh | bash

# Add the local bin directory to PATH so 'agy' is available immediately
ENV PATH="/home/vscode/.local/bin:${PATH}"