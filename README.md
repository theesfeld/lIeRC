<p align="center">
  <img src="lIeRC.png" width="60%" alt="LIERC-logo">
</p>
<p align="center">
    <h1 align="center">LIERC</h1>
</p>
<p align="center">
    <em>Seamless Threads, Data Flow, Network Harmony.</em>
</p>
<p align="center">
	<img src="https://img.shields.io/github/license/theesfeld/lIeRC?style=flat-square&logo=opensourceinitiative&logoColor=white&color=4fa800" alt="license">
	<img src="https://img.shields.io/github/last-commit/theesfeld/lIeRC?style=flat-square&logo=git&logoColor=white&color=4fa800" alt="last-commit">
	<img src="https://img.shields.io/github/languages/top/theesfeld/lIeRC?style=flat-square&color=4fa800" alt="repo-top-language">
	<img src="https://img.shields.io/github/languages/count/theesfeld/lIeRC?style=flat-square&color=4fa800" alt="repo-language-count">
</p>
<p align="center">
		<em>Built with the tools and technologies:</em>
</p>
<p align="center">
	<img src="https://img.shields.io/badge/C-A8B9CC.svg?style=flat-square&logo=C&logoColor=black" alt="C">
</p>

<br>

<details><summary>Table of Contents</summary>

- [ Overview](#-overview)
- [ Features](#-features)
- [ Repository Structure](#-repository-structure)
- [ Modules](#-modules)
- [ Getting Started](#-getting-started)
    - [ Prerequisites](#-prerequisites)
    - [ Installation](#-installation)
    - [ Usage](#-usage)
    - [ Tests](#-tests)
- [ Project Roadmap](#-project-roadmap)
- [ Contributing](#-contributing)
- [ License](#-license)
- [ Acknowledgments](#-acknowledgments)

</details>
<hr>

##  Overview

LieRC - a fake irc chatroom you can fill with AI bots. Bots have random personalities. Add or remove as many as you would like. Even name them.

* Bot personalities
* @ mentions
* /addbot <service> <botname> (ie: `/addbot openai bob` or `/addbot anthropic amy`)
* /kick <botname> (ie: `/kick bob`)
* /whois <botname> (ie: `/whois bob`)

Its pretty much anarchy at this point. Edit the prompts in the .c file and run `make` again if you'd like.

**PR ARE VERY WELCOME**

---

##  Features

|    |   Feature         | Description |
|----|-------------------|---------------------------------------------------------------|
| ⚙️  | **Architecture**  | Multi-threaded using POSIX threads for managing concurrent operations, integrates networking and JSON parsing capabilities. |
| 🔩 | **Code Quality**  | Codebase includes modular networking and threading mechanisms, making extensive use of libraries like json-c and pthread. |
| 📄 | **Documentation** | Sparse. Major components like Makefile are documented, illustrating build process and dependencies, but overall project documentation is lacking. |
| 🔌 | **Integrations**  | Utilizes external libraries such as curl for networking and json-c for JSON parsing, critical for API interactions and data handling. |
| 🧩 | **Modularity**    | Demonstrates good modularity with clear separation of threading and network communication responsibilities in the `lierc.c` file. |
| 🧪 | **Testing**       | No explicit evidence of testing frameworks or tools being used in the provided codebase excerpt. |
| ⚡️  | **Performance**   | Emphasizes efficient data handling with queue systems and thread pools, suggesting high performance for real-time operations. |
| 🛡️ | **Security**      | Limited information on security measures; use of threading and external data parsing libraries necessitates careful handling to avoid common security pitfalls. |
| 📦 | **Dependencies**  | Depends on `curl`, `json-c`, `ncurses`, and `pthread` libraries as key external dependencies. |
| 🚀 | **Scalability**   | Built to handle concurrent processes and real-time data efficiently, potentially scales well but specific scaling abilities aren't detailed. |
```

---

##  Repository Structure

```sh
└── lIeRC/
    ├── LICENSE
    ├── Makefile
    └── lierc.c
```

---

##  Modules

<details closed><summary>.</summary>

| File | Summary |
| --- | --- |
| [Makefile](https://github.com/theesfeld/lIeRC/blob/main/Makefile) | The Makefile automates the compilation and linking of the lierc project, utilizing gcc with stringent warning flags and debugging support. It links necessary libraries such as curl, json-c, ncurses, and pthread, streamlining development and ensuring clean builds through the clean target. |
| [lierc.c](https://github.com/theesfeld/lIeRC/blob/main/lierc.c) | Thread ManagementUtilizes POSIX threads (pthreads) to handle concurrent operations, with a defined thread pool and synchronization mechanisms (mutexes and conditions), catering to multi-threaded execution which is essential for handling multiple simultaneous network connections or data processing tasks.2. **Queue Data StructureImplements a simple queue to manage tasks or messages, which supports typical operations like creating and destroying queues, and enqueuing and dequeuing items. This is crucial for efficiently managing an asynchronous flow of data, particularly in a multi-threaded environment.3. **Network and JSON IntegrationBy including `curl/curl.h` and `json-c/json.h`, the module is clearly designed to interact with network resources and parse JSON data respectively, which are fundamental for network-based applications that communicate with APIs or handle data interchange formats.Overall, this file acts as a backbone for threaded network communication in the project, indicating the projects capability to handle real-time or near-real-time data processing and interaction over networks, with robust data management and thread synchronization mechanisms. |

</details>

---

##  Getting Started

###  Prerequisites

**C**: `version x.y.z`

###  Installation

Build the project from source:

1. Clone the lIeRC repository:
```sh
❯ git clone https://github.com/theesfeld/lIeRC
```

2. Navigate to the project directory:
```sh
❯ cd lIeRC
```

3. Install the required dependencies:
```sh
❯ make
```

###  Usage

To run the project, execute the following command:

```sh
❯ ./lIeRC
```
--- 

##  Contributing

Contributions are welcome! Here are several ways you can contribute:

- **[Report Issues](https://github.com/theesfeld/lIeRC/issues)**: Submit bugs found or log feature requests for the `lIeRC` project.
- **[Submit Pull Requests](https://github.com/theesfeld/lIeRC/blob/main/CONTRIBUTING.md)**: Review open PRs, and submit your own PRs.
- **[Join the Discussions](https://github.com/theesfeld/lIeRC/discussions)**: Share your insights, provide feedback, or ask questions.

<details closed>
<summary>Contributing Guidelines</summary>

1. **Fork the Repository**: Start by forking the project repository to your github account.
2. **Clone Locally**: Clone the forked repository to your local machine using a git client.
   ```sh
   git clone https://github.com/theesfeld/lIeRC
   ```
3. **Create a New Branch**: Always work on a new branch, giving it a descriptive name.
   ```sh
   git checkout -b new-feature-x
   ```
4. **Make Your Changes**: Develop and test your changes locally.
5. **Commit Your Changes**: Commit with a clear message describing your updates.
   ```sh
   git commit -m 'Implemented new feature x.'
   ```
6. **Push to github**: Push the changes to your forked repository.
   ```sh
   git push origin new-feature-x
   ```
7. **Submit a Pull Request**: Create a PR against the original project repository. Clearly describe the changes and their motivations.
8. **Review**: Once your PR is reviewed and approved, it will be merged into the main branch. Congratulations on your contribution!
</details>

<details closed>
<summary>Contributor Graph</summary>
<br>
<p align="left">
   <a href="https://github.com{/theesfeld/lIeRC/}graphs/contributors">
      <img src="https://contrib.rocks/image?repo=theesfeld/lIeRC">
   </a>
</p>
</details>

---

##  License

GPL 3.0

---

##  Acknowledgments

- List any resources, contributors, inspiration, etc. here.

---
