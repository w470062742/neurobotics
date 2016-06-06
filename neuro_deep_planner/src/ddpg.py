import numpy as np
import random
from collections import deque
from ou_noise import OUNoise
from critic import CriticNetwork
from actor import ActorNetwork


# Hyper Parameters:
REPLAY_BUFFER_SIZE = 10000  # How big can the buffer get
REPLAY_START_SIZE = 500     # When do we start training

BATCH_SIZE = 50             # How big are our batches

GAMMA = 0.99                # Discount factor

MU = 0.0                    # Center value of noise
THETA = 0.08                # Specifies how strong noise values are pulled towards mu
SIGMA = 0.04                # Variance of noise


class DDPG:

    def __init__(self):

        # Hardcode input size and action size
        self.height = 80
        self.width = self.depth
        self.depth = 4
        self.action_dim = 2

        # Initialize the current action and the old action for setting experiences
        self.action = np.zeros((2, 1), dtype='float')
        self.old_action = np.zeros((2, 1), dtype='float')

        # Initialize the old state for setting experiences
        self.old_state = np.zeros((self.depth, self.width, self.height), dtype='float')

        # Initialize actor and critic networks
        self.actor_network = ActorNetwork(self.height, self.action_dim, self.depth, BATCH_SIZE)
        self.critic_network = CriticNetwork(self.height, self.action_dim, self.depth, BATCH_SIZE)

        # Initialize replay buffer
        self.replay_buffer = deque()

        # Initialize a random process the Ornstein-Uhlenbeck process for action exploration
        self.exploration_noise = OUNoise(self.action_dim, MU, THETA, SIGMA)

        # Initialize time step
        self.time_step = 0

    def train(self):

        # Sample a random minibatch of N transitions from replay buffer
        minibatch = random.sample(self.replay_buffer, BATCH_SIZE)
        state_batch = [data[0] for data in minibatch]
        action_batch = [data[1] for data in minibatch]
        action_batch = np.resize(action_batch, [BATCH_SIZE, 1])
        reward_batch = [data[2] for data in minibatch]
        next_state_batch = [data[3] for data in minibatch]

        # Calculate y
        y_batch = []
        next_action_batch = self.actor_network.target_evaluate(next_state_batch)
        q_value_batch = self.critic_network.target_evaluate(next_state_batch, next_action_batch)
        for i in range(0, BATCH_SIZE):
            done = minibatch[i][4]
            if done:
                y_batch.append(reward_batch[i])
            else:
                y_batch.append(reward_batch[i] + GAMMA * q_value_batch[i])

        # Update critic by minimizing the loss L
        self.critic_network.train(y_batch, state_batch, action_batch)

        # Update the actor policy using the sampled gradient:
        action_batch_for_gradients = self.actor_network.evaluate(state_batch)
        q_gradient_batch = self.critic_network.gradients(state_batch, action_batch_for_gradients)

        self.actor_network.train(q_gradient_batch, state_batch)

        # Update the target networks TODO: DOit

    def get_action(self, state):

        # Select action a_t according to the current policy and exploration noise
        self.action = self.actor_network.get_action(state) + self.exploration_noise.noise()

        # TODO: Should we clip these values?
        return self.action

    def get_buffer_size(self):

        return len(self.replay_buffer)

    def set_experience(self, state, reward, done):

        # Store transition (s_t, a_t, r_t, s_{t+1}) in replay buffer
        self.replay_buffer.append((self.old_state, self.old_action, reward, state, done))

        # Safe old state and old action for next experience
        self.old_state = state
        self.old_action = self.action

        # Update time step
        self.time_step += 1

        # Limit the replay buffer size
        if len(self.replay_buffer) > REPLAY_BUFFER_SIZE:
            self.replay_buffer.popleft()